//
//  midio_apl.c
//
//  Created by Gabriele Mondada on May 23, 2021.
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
    char name[128];

    int device_id;
    int entity_id;
    int entity_extension_index;
    int input_endpoint_id;
    int output_endpoint_id;

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

    MIDIClientRef midiClient;
    MIDIPortRef inputPort;
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
    if (properties && CFGetTypeID(properties) == CFDictionaryGetTypeID()) {
        CFDictionaryRef dict = (CFDictionaryRef)properties;
        CFDictionaryApplyFunction(dict, _printProperty, (void *)prefix);
    }
    if (properties)
        CFRelease(properties);
}

static bool _is_iac_device(MIDIDeviceRef device)
{
    bool ret = false;
    CFPropertyListRef properties = NULL;
    MIDIObjectGetProperties(device, &properties, false);
    if (properties && CFGetTypeID(properties) == CFDictionaryGetTypeID()) {
        CFDictionaryRef dict = (CFDictionaryRef)properties;
        CFStringRef owner = CFDictionaryGetValue(dict, kMIDIPropertyDriverOwner);
        if (owner && CFGetTypeID(owner) == CFStringGetTypeID()) {
            if (CFStringCompare(owner, CFSTR("com.apple.AppleMIDIIACDriver"), 0) == kCFCompareEqualTo)
                ret = true;
        }
    }
    if (properties)
        CFRelease(properties);
    return ret;
}

static struct midio_port *_add_port(MIDIO *me, const char *name, int device_id, int entity_id, int entity_extension_index)
{
    struct midio_private *priv = (struct midio_private *)me;

    priv->port_count++;
    priv->ports = realloc(priv->ports, priv->port_count * sizeof(*priv->ports));
    struct midio_port *ret = &priv->ports[priv->port_count - 1];
    memset(ret, 0, sizeof(*ret));
    ret->midio = me;
    ret->index = priv->port_count - 1;
    strlcpy(ret->name, name, sizeof(ret->name));
    ret->device_id = device_id;
    ret->entity_id = entity_id;
    ret->entity_extension_index = entity_extension_index;
    return ret;
}

static void _add_or_update_port(MIDIO *me, int device_id, const char *device_name, int entity_id, const char *entity_name, int endpoint_id, bool is_input, MIDIEndpointRef endpoint)
{
    struct midio_private *priv = (struct midio_private *)me;

    int found_index = -1;
    int extension_count = 0;

    for (int i = 0; i < priv->port_count; i++) {
        struct midio_port *port = priv->ports + i;
        if (port->device_id == device_id && port->entity_id == entity_id) {
            extension_count++;
            if (port->input_endpoint_id == endpoint_id || port->output_endpoint_id == endpoint_id) {
                // endpoint already present - do nothing
                return;
            }
            if (is_input && port->input_endpoint_id == 0) {
                // found existing port having no input
                if (found_index == -1)
                    found_index = i;
            }
            if (!is_input && port->output_endpoint_id == 0) {
                // found existing port having no output
                if (found_index == -1)
                    found_index = i;
            }
        }
    }
    if (found_index >= 0) {
        if (is_input) {
            // add the input endpoint to the existing port
            struct midio_port *port = priv->ports + found_index;
            port->input_endpoint_id = endpoint_id;
            port->inputEndpoint = endpoint;
            port->inputPort = priv->inputPort;
        } else {
            // add the output endpoint to the existing port
            struct midio_port *port = priv->ports + found_index;
            port->output_endpoint_id = endpoint_id;
            port->outputEndpoint = endpoint;
            MIDIPortRef outputPort;
            OSStatus result = MIDIOutputPortCreate(priv->midiClient, CFSTR("Output"), &outputPort);
            if (result != noErr)
                _FATAL("result=%d", result);
            port->outputPort = outputPort;
        }
    } else {
        // create a new port (entity extension)
        char name[128];
        strlcpy(name, device_name, sizeof(name));
        if (strcmp(device_name, entity_name)) {
            strlcat(name, " - ", sizeof(name));
            strlcat(name, entity_name, sizeof(name));
        }
        if (extension_count > 0) {
            char count_str[128];
            snprintf(count_str, sizeof(count_str), "%d", extension_count);
            strlcat(name, " - Extension ", sizeof(name));
            strlcat(name, count_str, sizeof(name));
        }
        _add_port(me, name, device_id, entity_id, extension_count);
        _add_or_update_port(me, device_id, device_name, entity_id, entity_name, endpoint_id, is_input, endpoint);
    }
}

static void _add_virtual_output_port(MIDIO *me, const char *name, int fourcc)
{
    struct midio_private *priv = (struct midio_private *)me;

    OSStatus result;
    MIDIEndpointRef outputEndpoint;

    CFStringRef clientName = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);

    result = MIDISourceCreateWithProtocol(priv->midiClient, clientName, kMIDIProtocol_1_0, &outputEndpoint);
    if (result != noErr)
        _FATAL("result=%d", result);

    CFRelease(clientName);

    SInt32 outputId = 0;
    result = MIDIObjectGetIntegerProperty(outputEndpoint, kMIDIPropertyUniqueID, &outputId);
    if (result != noErr)
        _FATAL("result=%d", result);

    // TODO: how to manage conflicts?
    outputId = fourcc;

    result = MIDIObjectSetIntegerProperty(outputEndpoint, kMIDIPropertyUniqueID, outputId);
    if (result != noErr)
        _FATAL("result=%d", result);

    struct midio_port *port = _add_port(me, "Virtual Output", 0, outputId, 0);
    port->virtualOutputEndpoint = outputEndpoint;
}

static void _scan_for_physical_ports(MIDIO *me)
{
    OSStatus result;

    ItemCount numOfDevices = MIDIGetNumberOfDevices();
    for (ItemCount deviceIndex = 0; deviceIndex < numOfDevices; deviceIndex++) {
        MIDIDeviceRef device = MIDIGetDevice(deviceIndex);
        printf("device %d:\n", (int)deviceIndex);
        printf("  properties:\n");
        _printObjectProperties(device, "    ");

        if (_is_iac_device(device))
            continue;

        SInt32 deviceId = 0;
        result = MIDIObjectGetIntegerProperty(device, kMIDIPropertyUniqueID, &deviceId);
        if (result != noErr)
            continue;

        CFStringRef deviceName = NULL;
        CFPropertyListRef properties = NULL;
        MIDIObjectGetProperties(device, &properties, false);
        if (properties && CFGetTypeID(properties) == CFDictionaryGetTypeID()) {
            CFDictionaryRef dict = (CFDictionaryRef)properties;
            CFStringRef name = CFDictionaryGetValue(dict, kMIDIPropertyName);
            if (name && CFGetTypeID(name) == CFStringGetTypeID())
                deviceName = name;
        }

        char device_name[128];
        if (deviceName) {
            CFStringGetCString(deviceName, device_name, sizeof(device_name), kCFStringEncodingUTF8);
        } else {
            snprintf(device_name, sizeof(device_name), "Device %08x", deviceId);
        }

        if (properties)
            CFRelease(properties);

        ItemCount numOfEntities = MIDIDeviceGetNumberOfEntities(device);
        for (ItemCount entityIndex = 0; entityIndex < numOfEntities; entityIndex++) {
            MIDIEntityRef entity = MIDIDeviceGetEntity(device, entityIndex);
            printf("  entity %d:\n", (int)entityIndex);
            printf("    properties:\n");
            _printObjectProperties(entity, "      ");

            SInt32 entityId = 0;
            result = MIDIObjectGetIntegerProperty(entity, kMIDIPropertyUniqueID, &entityId);
            if (result != noErr)
                continue;

            CFStringRef entityName = NULL;
            CFPropertyListRef properties = NULL;
            MIDIObjectGetProperties(entity, &properties, false);
            if (properties && CFGetTypeID(properties) == CFDictionaryGetTypeID()) {
                CFDictionaryRef dict = (CFDictionaryRef)properties;
                CFStringRef name = CFDictionaryGetValue(dict, kMIDIPropertyName);
                if (name && CFGetTypeID(name) == CFStringGetTypeID())
                    entityName = name;
            }

            char entity_name[128];
            if (entityName) {
                CFStringGetCString(entityName, entity_name, sizeof(entity_name), kCFStringEncodingUTF8);
            } else {
                snprintf(entity_name, sizeof(entity_name), "Entity %08x", entityId);
            }

            if (properties)
                CFRelease(properties);

            ItemCount numOfSources = MIDIEntityGetNumberOfSources(entity);
            for (ItemCount sourceIndex = 0; sourceIndex < numOfSources; sourceIndex++) {
                MIDIEndpointRef endpoint = MIDIEntityGetSource(entity, sourceIndex);
                printf("    source %d:\n", (int)sourceIndex);
                printf("      properties:\n");
                _printObjectProperties(endpoint, "        ");

                SInt32 endpointId = 0;
                result = MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &endpointId);
                if (result != noErr)
                    continue;

                _add_or_update_port(me, deviceId, device_name, entityId, entity_name, endpointId, true, endpoint);
            }

            ItemCount numOfDestinations = MIDIEntityGetNumberOfDestinations(entity);
            for (ItemCount destinationIndex = 0; destinationIndex < numOfDestinations; destinationIndex++) {
                MIDIEndpointRef endpoint = MIDIEntityGetDestination(entity, destinationIndex);
                printf("    destination %d:\n", (int)destinationIndex);
                printf("      properties:\n");
                _printObjectProperties(endpoint, "        ");

                SInt32 endpointId = 0;
                result = MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &endpointId);
                if (result != noErr)
                    continue;

                _add_or_update_port(me, deviceId, device_name, entityId, entity_name, endpointId, false, endpoint);
            }
        }
    }
}

static void _scan_for_virtual_ports(MIDIO *me)
{
    struct midio_private *priv = (struct midio_private *)me;
    OSStatus result;

    ItemCount numOfSources = MIDIGetNumberOfSources();
    for (ItemCount sourceIndex = 0; sourceIndex < numOfSources; sourceIndex++) {
        MIDIEndpointRef sourceEndpoint = MIDIGetSource(sourceIndex);
        printf("device %d:\n", (int)sourceIndex);
        printf("  properties:\n");
        _printObjectProperties(sourceEndpoint, "    ");

        SInt32 entityId = 0;
        result = MIDIObjectGetIntegerProperty(sourceEndpoint, kMIDIPropertyUniqueID, &entityId);
        if (result != noErr)
            continue;

        int index = -1;
        for (int i = 0; i < priv->port_count; i++) {
            struct midio_port *port = priv->ports + i;
            if (port->device_id == 0 && port->entity_id == entityId) {
                index = i;
                break;
            }
        }
        if (index >= 0) {
            struct midio_port *port = priv->ports + index;
            if (port->virtualOutputEndpoint)
                continue;
            if (port->inputEndpoint != sourceEndpoint) {
                port->inputEndpoint = sourceEndpoint;
                // TODO: reconnect input (possible only once auto-detection will be implemented)
                continue;
            }
        }

        CFStringRef entityName = NULL;
        CFPropertyListRef properties = NULL;
        MIDIObjectGetProperties(sourceEndpoint, &properties, false);
        if (properties && CFGetTypeID(properties) == CFDictionaryGetTypeID()) {
            CFDictionaryRef dict = (CFDictionaryRef)properties;
            CFStringRef name = CFDictionaryGetValue(dict, kMIDIPropertyName);
            if (name && CFGetTypeID(name) == CFStringGetTypeID())
                entityName = name;
        }

        char entity_name[128];
        if (entityName) {
            CFStringGetCString(entityName, entity_name, sizeof(entity_name), kCFStringEncodingUTF8);
        } else {
            snprintf(entity_name, sizeof(entity_name), "Source %08x", entityId);
        }

        struct midio_port *port = _add_port(me, entity_name, 0, entityId, 0);
        port->input_endpoint_id = entityId;
        port->inputEndpoint = sourceEndpoint;
        port->inputPort = priv->inputPort;
    }
}

MIDIO *midio_create(void)
{
    struct midio_private *me = calloc(1, sizeof(*me));
    return &me->public;
}

void midio_destroy(MIDIO *me)
{
    struct midio_private *priv = (struct midio_private *)me;
    assert(priv->midiClient == 0);
    free(me);
}

void midio_open(MIDIO *me)
{
    struct midio_private *priv = (struct midio_private *)me;
    OSStatus result;

    assert(priv->midiClient == 0);

    result = MIDIClientCreate(CFSTR("MIDI client GMO"), NULL, NULL, &priv->midiClient);
    if (result != noErr)
        _FATAL("result=%d", result);

    result = MIDIInputPortCreateWithProtocol(priv->midiClient, CFSTR("Input"), kMIDIProtocol_1_0, &priv->inputPort, ^(const MIDIEventList *eventList, void *srcConnRefCon)
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

    // TODO: move these literals outside the module
    _add_virtual_output_port(me, "miditrick", 'MTRK');

    _scan_for_physical_ports(me);
    _scan_for_virtual_ports(me);

    for (int i = 0; i < priv->port_count; i++) {
        printf("device %d: name=%s\n", i, priv->ports[i].name);
    }
}

void midio_close(MIDIO *me)
{
    // TODO: disconnect input ports
    // TODO: dispose output ports
    // TODO: dispose virtual output ports
    // TODO: dispose input port
    // TODO: dispose midi client
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
