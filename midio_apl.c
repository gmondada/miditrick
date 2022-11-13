//
//  midio_apl.c
//
//  Created by Gabriele Mondada on 23.05.21.
//  Copyright (c) 2021 Gabriele Mondada.
//  Distributed under the terms of the MIT License.
//

#include <stdlib.h>
#include "midio.h"
#include <CoreMIDI/CoreMIDI.h>


/*** literals ***/

#define _FATAL(format, ...) \
    do { _fatal("midio: fatal error (%s:%d): " format, __FILE__, __LINE__, ##__VA_ARGS__); } while(0)


/*** types ***/

struct midio_port {
    struct midio *midio;
    int index;
    char name[64];

    // physical or virtual input
    MIDIEndpointRef inputEndpoint;
    MIDIPortRef inputPort;

    // physical output
    MIDIEndpointRef outputEndpoint;
    MIDIPortRef outputPort;

    // virtual output
    MIDIEndpointRef virtualOutputEndpoint;
};

struct midio_private {
    struct midio public;

    void (* recv_handler)(void *ctx, MIDIO_MSG *msg);
    void *recv_handler_ctx;

    struct midio_port *ports;
    int port_count;
};


/*** functions ***/

static void _fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    abort();
}

static CFStringRef _convertCFNumberToCFString(CFNumberRef value)
{
    CFNumberFormatterRef formatter = CFNumberFormatterCreate(kCFAllocatorDefault, NULL, kCFNumberFormatterNoStyle);
    CFStringRef desc = CFNumberFormatterCreateStringWithNumber(kCFAllocatorDefault, formatter, value);
    CFRelease(formatter);
    return desc;
}

static void _printProperty(const void *key, const void *value, void *context)
{
    const char *prefix = context;
    CFStringRef keyDesc;
    if (CFGetTypeID(key) == CFStringGetTypeID())
        keyDesc = CFRetain(key);
    else
        keyDesc = CFCopyDescription(key);
    char key_desc[1024];
    CFStringGetCString(keyDesc, key_desc, sizeof(key_desc), kCFStringEncodingUTF8);
    CFStringRef valueDesc;
    if (CFGetTypeID(value) == CFStringGetTypeID())
        valueDesc = CFRetain(value);
    else if (CFGetTypeID(value) == CFNumberGetTypeID())
        valueDesc = _convertCFNumberToCFString(value);
    else
        valueDesc = CFCopyDescription(value);
    char value_desc[1024];
    CFStringGetCString(valueDesc, value_desc, sizeof(value_desc), kCFStringEncodingUTF8);
    printf("%s%s: %s\n", prefix, key_desc, value_desc);
    CFRelease(keyDesc);
    CFRelease(valueDesc);
}

static void _printObjectProperties(MIDIObjectRef obj, const char *prefix)
{
    CFPropertyListRef properties = NULL;
    MIDIObjectGetProperties(obj, &properties, false);

    CFTypeID typeId = CFGetTypeID(properties);
    if (typeId == CFDictionaryGetTypeID()) {
        CFDictionaryRef dict = (CFDictionaryRef)properties;
        CFDictionaryApplyFunction(dict, _printProperty, (void *)prefix);
    }
}

static struct midio_port *_add_port(MIDIO *me)
{
    struct midio_private *priv = (struct midio_private *)me;

    priv->port_count++;
    priv->ports = realloc(priv->ports, priv->port_count * sizeof(*priv->ports));
    struct midio_port *ret = &priv->ports[priv->port_count - 1];
    memset(ret, 0, sizeof(*ret));
    ret->midio = me;
    ret->index = priv->port_count - 1;
    return ret;
}

MIDIO *midio_create(void)
{
    struct midio_private *me = calloc(1, sizeof(*me));
    return &me->public;
}

void midio_open(MIDIO *me)
{
    struct midio_private *priv = (struct midio_private *)me;

    MIDIClientRef midiClient;
    MIDIEndpointRef outputEndpoint;

    OSStatus result;

    result = MIDIClientCreate(CFSTR("MIDI client GMO"), NULL, NULL, &midiClient);
    if (result != noErr)
        _FATAL("result=%d", result);

    result = MIDISourceCreateWithProtocol(midiClient, CFSTR("Miditrick"), kMIDIProtocol_1_0, &outputEndpoint);
    if (result != noErr)
        _FATAL("result=%d", result);

    SInt32 outputId = 0;
    result = MIDIObjectGetIntegerProperty(outputEndpoint, kMIDIPropertyUniqueID, &outputId);
    if (result != noErr)
        _FATAL("result=%d", result);

    // TODO: store it persistently
    outputId = 'MTRK';

    result = MIDIObjectSetIntegerProperty(outputEndpoint, kMIDIPropertyUniqueID, outputId);
    if (result != noErr)
        _FATAL("result=%d", result);

    struct midio_port *output_port = _add_port(me);
    strlcpy(output_port->name, "Virtual Output", sizeof(output_port->name));
    output_port->virtualOutputEndpoint = outputEndpoint;

    MIDIPortRef inputPort;

    result = MIDIInputPortCreateWithProtocol(midiClient, CFSTR("Input"), kMIDIProtocol_1_0, &inputPort, ^(const MIDIEventList *eventList, void *srcConnRefCon)
    {
        struct midio_port *port = srcConnRefCon;
        struct midio_private *priv = (struct midio_private *)port->midio;

        const MIDIEventPacket *packet = eventList->packet;

        for (int i = 0; i < eventList->numPackets; i++) {
            if (packet->wordCount >= 1 && (packet->words[0] >> 24) == 0x20) {
                UInt32 word = packet->words[0];
                MIDIO_MSG msg = {
                    .port = port->index,
                    .size = 3,
                    .u8 = { word >> 16, word >> 8, word >> 0 },
                };
                if (priv->recv_handler) {
                    priv->recv_handler(priv->recv_handler_ctx, &msg);
                }
            }
            // move to next packet
            packet = MIDIEventPacketNext(packet);
        }
    });

    CFStringRef missingName = CFStringCreateWithCString(nil, "???", kCFStringEncodingUTF8);

    ItemCount numOfDevices = MIDIGetNumberOfDevices();
    for (ItemCount deviceIndex = 0; deviceIndex < numOfDevices; deviceIndex++) {
        MIDIDeviceRef device = MIDIGetDevice(deviceIndex);
        printf("device %d:\n", (int)deviceIndex);
        printf("  properties:\n");
        _printObjectProperties(device, "    ");

        CFStringRef deviceName = missingName;
        CFPropertyListRef properties = NULL;
        MIDIObjectGetProperties(device, &properties, false);
        if (properties && CFGetTypeID(properties) == CFDictionaryGetTypeID()) {
            CFDictionaryRef dict = (CFDictionaryRef)properties;
            CFStringRef name = CFDictionaryGetValue(dict, kMIDIPropertyName);
            if (name && CFGetTypeID(name) == CFStringGetTypeID())
                deviceName = name;
        }

        ItemCount numOfEntities = MIDIDeviceGetNumberOfEntities(device);
        for (ItemCount entityIndex = 0; entityIndex < numOfEntities; entityIndex++) {
            MIDIEntityRef entity = MIDIDeviceGetEntity(device, entityIndex);
            printf("  entity %d:\n", (int)entityIndex);
            printf("    properties:\n");
            _printObjectProperties(entity, "      ");
            CFStringRef entityName = NULL;
            result = MIDIObjectGetStringProperty(entity, kMIDIPropertyName, &entityName);
            if (result != noErr)
                _FATAL("result=%d", result);

            CFStringRef fullEntityName = NULL;
            if (numOfEntities == 1) {
                fullEntityName = deviceName;
                CFRetain(fullEntityName);
            } else {
                fullEntityName = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@:%@"), deviceName, entityName);
            }

            ItemCount numOfSources = MIDIEntityGetNumberOfSources(entity);
            for (ItemCount sourceIndex = 0; sourceIndex < numOfSources; sourceIndex++) {
                MIDIEndpointRef endpoint = MIDIEntityGetSource(entity, sourceIndex);
                printf("    source %d:\n", (int)sourceIndex);
                printf("      properties:\n");
                _printObjectProperties(endpoint, "        ");

                CFStringRef name;
                if (numOfSources == 1) {
                    name = fullEntityName;
                    CFRetain(name);
                } else {
                    CFStringRef endpointName = NULL;
                    result = MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &endpointName);
                    if (result != noErr)
                        _FATAL("result=%d", result);
                    name = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@:%@"), fullEntityName, endpointName);
                }

                struct midio_port *input_port = _add_port(me);
                CFStringGetCString(name, input_port->name, sizeof(input_port->name), kCFStringEncodingUTF8);
                input_port->inputEndpoint = endpoint;
                input_port->inputPort = inputPort;

                CFRelease(name);
            }
            ItemCount numOfDestinations = MIDIEntityGetNumberOfDestinations(entity);
            for (ItemCount destinationIndex = 0; destinationIndex < numOfDestinations; destinationIndex++) {
                MIDIEndpointRef endpoint = MIDIEntityGetDestination(entity, destinationIndex);
                printf("    destination %d:\n", (int)destinationIndex);
                printf("      properties:\n");
                _printObjectProperties(endpoint, "        ");

                CFStringRef name;
                if (numOfDestinations == 1) {
                    name = fullEntityName;
                    CFRetain(name);
                } else {
                    CFStringRef endpointName = NULL;
                    result = MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &endpointName);
                    if (result != noErr)
                        _FATAL("result=%d", result);
                    name = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@:%@"), fullEntityName, endpointName);
                }

                struct midio_port *output_port = NULL;
                char name_buf[sizeof(output_port->name)] = { 0 };
                CFStringGetCString(name, name_buf, sizeof(name_buf), kCFStringEncodingUTF8);
                int index = midio_get_port_by_name(me, name_buf);
                if (index == -1) {
                    output_port = _add_port(me);
                    CFStringGetCString(name, output_port->name, sizeof(output_port->name), kCFStringEncodingUTF8);
                } else {
                    output_port = &priv->ports[index];
                }

                MIDIPortRef outputPort;

                result = MIDIOutputPortCreate(midiClient, CFSTR("Output"), &outputPort);
                if (result != noErr)
                    _FATAL("result=%d", result);

                output_port->outputEndpoint = endpoint;
                output_port->outputPort = outputPort;
            }

            CFRelease(fullEntityName);
        }
    }

    CFRelease(missingName);

    for (int i = 0; i < priv->port_count; i++) {
        printf("device %d: name=%s\n", i, priv->ports[i].name);
    }
}

void midio_close(MIDIO *me)
{
    // TODO
}

int midio_get_port_by_name(MIDIO *me, const char *name)
{
    struct midio_private *priv = (struct midio_private *)me;

    for (int i = 0; i < priv->port_count; i++) {
        if (!strcmp(priv->ports[i].name, name))
            return i;
    }
    return -1;
}

void midio_start_pump(MIDIO *me, void *ctx, void (* handler)(void *ctx, MIDIO_MSG *msg))
{
    struct midio_private *priv = (struct midio_private *)me;

    priv->recv_handler = handler;
    priv->recv_handler_ctx = ctx;

    for (int i = 0; i < priv->port_count; i++) {
        struct midio_port *port = &priv->ports[i];

        if (port->inputPort) {
            OSStatus result = MIDIPortConnectSource(port->inputPort, port->inputEndpoint, port);
            if (result != noErr)
                _FATAL("result=%d", result);
        }
    }
}

static void _send(struct midio_port *port, MIDIO_MSG *msg)
{
    MIDIEventList eventList;

    if (msg->size == 3) {
        eventList.protocol = kMIDIProtocol_1_0;
        eventList.numPackets = 1;
        eventList.packet[0].timeStamp = 0; // now
        eventList.packet[0].wordCount = 1;
        eventList.packet[0].words[0] = 0x20000000 |
            (uint32_t)msg->u8[0] << 16 |
            (uint32_t)msg->u8[1] << 8 |
            (uint32_t)msg->u8[2] << 0;

        // printf("word out: 0x%08x\n", eventList.packet[0].words[0]);
    } else {
        printf("midio: output: ignoring unsupported message: ");
        midio_print_msg(msg);
        return;
    }

    if (port->outputPort) {
        OSStatus result = MIDISendEventList(port->outputPort, port->outputEndpoint, &eventList);
        if (result != noErr)
            _FATAL("result=%d", result);
    }

    if (port->virtualOutputEndpoint) {
        OSStatus result = MIDIReceivedEventList(port->virtualOutputEndpoint, &eventList);
        if (result != noErr)
            _FATAL("result=%d", result);
    }
}

static void _send_sysex_completion(MIDISysexSendRequest *request)
{
    // data point now tto the end of the buffer and size is zero
    assert(request->bytesToSend == 0);
    free((void *)request->completionRefCon);
    free(request);
}

static void _send_sysex(struct midio_port *port, const void *data, size_t size)
{
    unsigned char *buf = calloc(1, size);
    memcpy(buf, data, size);
    MIDISysexSendRequest *req = calloc(1, sizeof(MIDISysexSendRequest));
    req->destination = port->outputEndpoint;
    req->bytesToSend = (UInt32)size;
    req->data = buf;
    req->completionProc = _send_sysex_completion;
    req->completionRefCon = buf;
    OSStatus result = MIDISendSysex(req);
    if (result != noErr)
        _FATAL("result=%d", result);
}

void midio_recv(MIDIO *me, MIDIO_MSG *msg)
{
    _FATAL("not implemented");
}

void midio_send(MIDIO *me, MIDIO_MSG *msg)
{
    struct midio_private *priv = (struct midio_private *)me;

    if (msg->port == -1) {
        for (int i = 0; i < priv->port_count; i++) {
            struct midio_port *port = &priv->ports[i];
            _send(port, msg);
        }
    } else if (msg->port >= 0 && msg->port < priv->port_count) {
        struct midio_port *port = &priv->ports[msg->port];
        _send(port, msg);
    }
}

void midio_send_sysex(MIDIO *me, int port_nb, const void *data, size_t size)
{
    struct midio_private *priv = (struct midio_private *)me;

    if (port_nb == -1) {
        for (int i = 0; i < priv->port_count; i++) {
            struct midio_port *port = &priv->ports[i];
            _send_sysex(port, data, size);
        }
    } else if (port_nb >= 0 && port_nb < priv->port_count) {
        struct midio_port *port = &priv->ports[port_nb];
        _send_sysex(port, data, size);
    }
}
