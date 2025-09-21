#!/usr/bin/env python3

import usb1
import usb.core
import usb.util
import threading
import time
import struct
import argparse
from itertools import islice

# Userspace application for communicating with the 
# Womble Industries Optical Controller (WIOC) v1
#
# Uses a mix of python-libusb1 and pyusb, because pyusb doesn't
# yet support hotplug.
#
# Streams readings from the WIOC and converts them to OSC events and
# sends them to the OSC on the IP and port provided on the commandline.

REQ_PERIOD = 0x14
REQ_START = 0x13
REQ_STOP = 0x15
REQ_REST_VALUE = 0x16

# Mapping of USB port to OSC controller number (laptop ports)
#portmap = { 1 : 1, 2 : 0 }
portmap = { 2 : 1, 4 : 0 }

def handle_device(context, _device, stop_event):

    print(f'Handling device {_device} on port {_device.getPortNumber()}')

    # Find all of the pyusb devices matching the libusb1 event vendor and device
    devs = usb.core.find(idVendor=_device.getVendorID(), idProduct=_device.getProductID(), find_all=True)
    if devs is None:
        return

    dev = None
    iface = None
    ifnum = None
    port = _device.getPortNumber()

    # Find specific pyusb controller, matching device bus and address to the libusb1 hotplug event
    for device in devs:
        if device.bus == _device.getBusNumber() and device.address == _device.getDeviceAddress():
            for cfg in device:
                for interface in cfg:
                    # Vendor and product ID are generic, so we have a specific named interface to look for
                    print(f'Interface name {usb.util.get_string(device, interface.iInterface)}')
                    if usb.util.get_string(device, interface.iInterface) == 'WIPPv1':
                        dev = device
                        iface = interface
                        ifnum = interface.bInterfaceNumber

    if dev is None:
        return

    try:
        usb.util.claim_interface(dev, iface)
    except usb.core.USBError as e:
        print("Error occurred claiming " + str(e))
        return

    pixels = 12

    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",0,1,pixels))
    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",1,1,pixels))
    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",2,1,pixels))
    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",3,1,pixels))
    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",4,1,pixels))
    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",5,1,pixels))
    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",6,1,pixels))
    dev.ctrl_transfer(usb.TYPE_VENDOR | usb.RECIP_INTERFACE, 1, 0, ifnum, struct.pack("<BBH",7,1,pixels))

    endpt = iface.endpoints()[0]

    val = 0
    start_ms = time.time() * 1000
    while True:
        val += 1
        if val > 255:
            val = 0
            end_ms = time.time() * 1000
            delta_ms = end_ms - start_ms
            start_ms = end_ms
            print(f'FPS: {255 / (delta_ms / 1000)}')

        data = [ val ] * pixels * 3
        for i in range(8):
            hdr = [i]
            buf = hdr + data
            endpt.write(bytes(buf))

    #for i in range(0, 10):
        #endpt.write(jim)

    """
    # Configure the new device
    dev.ctrl_transfer(0x21, REQ_PERIOD, 0, ifnum, struct.pack("<L",15))
    #if port == 1:
        #dev.ctrl_transfer(0x21, REQ_REST_VALUE, 0, ifnum, struct.pack("<L",150))
    dev.ctrl_transfer(0x21, REQ_START, 0, ifnum, b"")

    # Read sensor data from the endpoint continuously, sending it out over OSC
    # until signalled by hotplug event or process exit to stop.
    while not stop_event.is_set():
        try:
            result = endpt.read(300, timeout=1000)
        except usb.core.USBTimeoutError as e:
            print('USB timeout error')
            continue
        except usb.core.USBError as e:
            print('USB error occurred, thread exiting ' + str(e))
            return

        for each in zip(*[iter(result)] * 3):
            mm, valid = struct.unpack("<HB", bytes(each))
            if valid or not out_of_range:
                print(f'/controller/pot{portmap[port]}', (mm/10, valid))
                osc_client.send_message(f'/controller/pot{portmap[port]}', (mm/10, valid))
                out_of_range = True if not valid else False

    dev.ctrl_transfer(0x21, REQ_STOP, 0, ifnum, b'')
    """

    print(f'thread exiting')

    return

threads = {}

def hotplug_callback(context, device, event):
    print("Device %s: %s" % (
        {
            usb1.HOTPLUG_EVENT_DEVICE_ARRIVED: 'arrived',
            usb1.HOTPLUG_EVENT_DEVICE_LEFT: 'left',
        }[event],
        device,
    ))

    if event == usb1.HOTPLUG_EVENT_DEVICE_ARRIVED:
        # Spawn a new thread for the newly inserted controller
        e = threading.Event()
        t = threading.Thread(target=handle_device, args=(context, device, e))
        threads[device] = (t, e)
        t.start()
    elif event == usb1.HOTPLUG_EVENT_DEVICE_LEFT:
        # Signal the thread handling the controller that it should exit
        t = threads[device][0]
        e = threads[device][1]
        e.set()
        t.join()

def main():
    parser = argparse.ArgumentParser()
    args = parser.parse_args()

    with usb1.USBContext() as context:
        if not context.hasCapability(usb1.CAP_HAS_HOTPLUG):
            print('Hotplug support is missing. Please update your libusb version.')
            return

        context.hotplugRegisterCallback(hotplug_callback,
            vendor_id=0xcafe, product_id=0x4001)

        print('Monitoring USB events, ^C to exit')

        try:
            while True:
                context.handleEvents()
        except (KeyboardInterrupt, SystemExit):
            for (t, e) in threads.values():
                e.set()
                t.join()
            print('Exiting')

if __name__ == '__main__':
    main()

