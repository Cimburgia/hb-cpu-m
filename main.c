#include <stdio.h>

#include <COreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

void m1_cpu_frequency() {
    unsigned int curr;
    unsigned int curr_g;
    uint32_t min = UINT32_MAX;
    uint32_t max = 0;
    CFDictionaryRef matching = IOServiceMatching("AppleARMIODevice");
    io_iterator_t iter;

    IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
    io_registry_entry_t entry;
    while ((entry = IOIteratorNext(iter))) {
        io_name_t name;
        IORegistryEntryGetName(entry, name);
        if (strncmp(name, "pmgr", 4) == 0) {
            break;
        }
    }
    
    IOObjectRelease(iter);
    
    
    CFTypeRef pCoreRef = IORegistryEntryCreateCFProperty(entry, CFSTR("voltage-states5-sram"), kCFAllocatorDefault, 0);
    
    size_t length = CFDataGetLength(pCoreRef);
    for (size_t i = 0; i < length - 3; i += 4) {
        uint32_t curr_freq = 0;
        CFDataGetBytes(pCoreRef, CFRangeMake(i, sizeof(uint32_t)), (UInt8 *) &curr_freq);
        if (curr_freq > 1e6 && curr_freq < min) {
            min = curr_freq;
        }
        if (curr_freq > max) {
            max = curr_freq;
        }
    }
    curr = max;
    curr_g = curr / 1000000;
    printf("Current CPU: %u MHz\n", curr_g);
}


int main() {
    m1_cpu_frequency();
    return 0;
}
