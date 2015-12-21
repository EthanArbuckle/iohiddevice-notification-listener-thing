#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDEventSystem.h>
#include <IOKit/IOKitLib.h>
#import <IOKit/IODataQueueClient.h>
#include <pthread.h>
#include <xpc/xpc.h>
#include <dlfcn.h>

IODataQueueMemory *dataPool;
uint32_t dataPoolSize;
pthread_t dataPoolThread;

mach_vm_address_t address;
mach_vm_size_t size;
mach_port_t machPort;

static void *dataPoolBucket(void *drop) {

    kern_return_t kr;

    NSLog(@"event receieved. (%p)", drop);

    char *poolBuffer = (char *)malloc(dataPoolSize);
    while (IODataQueueWaitForAvailableData(dataPool, machPort) == kIOReturnSuccess) {

        while (IODataQueueDataAvailable(dataPool)) {

            kr = IODataQueueDequeue(dataPool, poolBuffer, &dataPoolSize);
            if (kr != KERN_SUCCESS) {
                NSLog(@"error dequeuing");
            }

            NSLog(@"data pooled %s - size: %d", poolBuffer, dataPoolSize);
        }

    }

    kr = IODataQueueWaitForAvailableData(dataPool, machPort);
    NSLog(@"Stopping with error 0x%08x\n", kr);

    return 0;
}

int main(int argc, char **argv, char **envp) {

    kern_return_t kr;
    io_iterator_t iterator;
    io_service_t server;
    io_connect_t connect;

    io_name_t name;
    io_string_t path;

    kr = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("AGXSharedUserClient"), &iterator);
    if (kr != KERN_SUCCESS) {
        NSLog(@"failed 0x%08x\n", kr);
        return 0;
    }

    NSLog(@"iterator: %d", iterator);

    while ((server = IOIteratorNext(iterator))) {

        kr = NULL, connect = 0, dataPool = NULL, dataPoolSize = 0;
        dataPoolThread = NULL, size = 0, address = 0;

        NSLog(@"starting\n");

        IORegistryEntryGetName(server, name);
        IORegistryEntryGetPath(server, kIOServicePlane, path);
        NSLog(@"%s -- %s", name, path);

        kr = IOServiceOpen(server, mach_task_self(), 0, &connect);
        if (kr != KERN_SUCCESS) {
            NSLog(@"failed opening service 0x%08x\n", kr);
            IOServiceClose(connect);
            continue;
        }

        NSLog(@"connected %u", connect);

        machPort = IODataQueueAllocateNotificationPort();
        if (machPort == MACH_PORT_NULL) {
            NSLog(@"null mach port\n");
            IOServiceClose(connect);
            continue;
        }

        kr = IOConnectSetNotificationPort(connect, kIODefaultMemoryType, machPort, 0);
        if (kr != KERN_SUCCESS) {
            NSLog(@"failed setting port %d\n", kr);
            IOServiceClose(connect);
            continue;
        }

        kr = IOConnectMapMemory(connect, kIODefaultMemoryType, mach_task_self(), &address, &size, kIOMapAnywhere);
        if (kr != KERN_SUCCESS) {
            NSLog(@"failed mapping memory 0x%08x\n", kr);
            IOServiceClose(connect);
            continue;
        }

        dataPool = (IODataQueueMemory *)address;
        dataPoolSize = (uint32_t)size;

        int spawnedThread = pthread_create(&dataPoolThread, NULL, &dataPoolBucket, NULL);
        if (spawnedThread != 0) {
            NSLog(@"thread creation failed %d", spawnedThread);
            IOServiceClose(connect);
            continue;
        }

        NSLog(@"Spawned notification port");

        sleep(-1);

        IOServiceClose(connect);

    }

	return 0;
}
