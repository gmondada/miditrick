//
//  midio_apl.c
//  miditrick
//
//  Created by Gabriele Mondada on 23.05.21.
//

#include <stdlib.h>
#include "midio.h"
#include <CoreMIDI/CoreMIDI.h>


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

static void _fatal_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    abort();
}

static void _printProperty(const void *key, const void *value, void *context)
{
    const char *prefix = context;
    CFStringRef keyDesc = CFCopyDescription(key);
    char key_desc[1024];
    CFStringGetCString(keyDesc, key_desc, sizeof(key_desc), kCFStringEncodingUTF8);
    CFStringRef valueDesc = CFCopyDescription(value);
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
        _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);

    result = MIDISourceCreateWithProtocol(midiClient, CFSTR("Miditrick"), kMIDIProtocol_1_0, &outputEndpoint);
    if (result != noErr)
        _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);

    SInt32 outputId = 0;
    result = MIDIObjectGetIntegerProperty(outputEndpoint, kMIDIPropertyUniqueID, &outputId);
    if (result != noErr)
        _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);

    // TODO: store it persistently
    outputId = 'MTRK';

    result = MIDIObjectSetIntegerProperty(outputEndpoint, kMIDIPropertyUniqueID, outputId);
    if (result != noErr)
        _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);

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
            packet = (const MIDIEventPacket *)&packet->words[packet->wordCount];
        }
    });

    ItemCount numOfDevices = MIDIGetNumberOfDevices();
    for (ItemCount deviceIndex = 0; deviceIndex < numOfDevices; deviceIndex++) {
        MIDIDeviceRef device = MIDIGetDevice(deviceIndex);
        printf("device %d:\n", (int)deviceIndex);
        printf("  properties:\n");
        _printObjectProperties(device, "    ");
        ItemCount numOfEntities = MIDIDeviceGetNumberOfEntities(device);
        for (ItemCount entityIndex = 0; entityIndex < numOfEntities; entityIndex++) {
            MIDIEntityRef entity = MIDIDeviceGetEntity(device, entityIndex);
            printf("  entity %d:\n", (int)entityIndex);
            printf("    properties:\n");
            _printObjectProperties(entity, "      ");
            CFStringRef entityName = NULL;
            result = MIDIObjectGetStringProperty(entity, kMIDIPropertyName, &entityName);
            if (result != noErr)
                _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);
            ItemCount numOfSources = MIDIEntityGetNumberOfSources(entity);
            for (ItemCount sourceIndex = 0; sourceIndex < numOfSources; sourceIndex++) {
                MIDIEndpointRef endpoint = MIDIEntityGetSource(entity, sourceIndex);
                printf("    source %d:\n", (int)sourceIndex);
                printf("      properties:\n");
                _printObjectProperties(endpoint, "        ");

                CFStringRef name;
                if (numOfSources == 1) {
                    name = entityName;
                    CFRetain(name);
                } else {
                    CFStringRef endpointName = NULL;
                    result = MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &endpointName);
                    if (result != noErr)
                        _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);
                    name = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@: %@"), entityName, endpointName);
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
                    name = entityName;
                    CFRetain(name);
                } else {
                    CFStringRef endpointName = NULL;
                    result = MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &endpointName);
                    if (result != noErr)
                        _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);
                    name = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@: %@"), entityName, endpointName);
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
                    _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);

                output_port->outputEndpoint = endpoint;
                output_port->outputPort = outputPort;
            }
        }
    }
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
                _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);
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
            _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);
    }

    if (port->virtualOutputEndpoint) {
        OSStatus result = MIDIReceivedEventList(port->virtualOutputEndpoint, &eventList);
        if (result != noErr)
            _fatal_error("%s(%d): error=%d", __FILE__, __LINE__, result);
    }
}

void midio_recv(MIDIO *me, MIDIO_MSG *msg)
{
    _fatal_error("%s(%d): not implemented", __FILE__, __LINE__);
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
