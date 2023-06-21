#include <stdio.h>

#include <COreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

void m1_cpu_frequency() {
    unsigned int curr;
    unsigned int curr_g;
    uint32_t min = UINT32_MAX;
    uint32_t max = 0;
    
    // Get service type properties and general utility functions
    CFDictionaryRef matching = IOServiceMatching("AppleARMIODevice");
    io_iterator_t iter;

    // Returns an iterator for IOService objects
    IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
    io_registry_entry_t entry;
    
    // Loop through IOService entries until pmgr is found
    while ((entry = IOIteratorNext(iter))) {
        io_name_t name;
        IORegistryEntryGetName(entry, name);
        //printf("Entry Name: %s\n", name);
        if (strncmp(name, "pmgr", 4) == 0) {
            break;
        }
    }

    IOObjectRelease(iter);
    
    
    CFTypeRef pCoreRef = IORegistryEntryCreateCFProperty(entry, CFSTR("voltage-states5-sram"), kCFAllocatorDefault, 0);
    
    size_t length = CFDataGetLength(pCoreRef);
    for (size_t i = 0; i < length - 3; i += 4) {
        uint32_t curr_freq = 0;
        // Get all values here
        CFDataGetBytes(pCoreRef, CFRangeMake(i, sizeof(uint32_t)), (UInt8 *) &curr_freq);
        printf("curr: %u\n", curr_freq);
        if (curr_freq > 1e6 && curr_freq < min) {
            min = curr_freq;
            //printf("min: %u\n", min);
        }
        if (curr_freq > max) {
            max = curr_freq;
            //printf("max: %u\n", max);
        }
    }
    curr = max;
    curr_g = curr / 1000000;
    //printf("Current CPU: %u MHz\n", curr_g);
}


int main() {
    m1_cpu_frequency();
    return 0;
}

/// Thread that increments counter
/// Every 1000 iterations check time elapsed
/// In same thread
/// run in different environments:
///     power, no power, under streaa
/// Make sure you get the most precise time