
#import "main.h"
/*
    Notes:

    make sure data collection knows when it failed:
        - scheduled on core it wasn't supposed to run
        - or switch cores
    if this happens throw away that run

    is there a way to see what core you are running on?
*/
/* data structs
*/
typedef struct unit_data {
    //int percluster_ncores[];
    
    struct {
        IOReportSubscriptionRef cpu_sub;
        CFMutableDictionaryRef cpu_sub_chann;
        CFMutableDictionaryRef cpu_chann;
    } soc_samples;
} unit_data;

static CFStringRef ptype_state = CFSTR("P");
static CFStringRef vtype_state = CFSTR("V");

/* defs
 */
static inline void init_unit_data(unit_data* data);
static void sample(unit_data* unit_data);
static inline void get_core_counts(unit_data* unit_data);

static inline void init_unit_data(unit_data* data) {
    memset((void*)data, 0, sizeof(unit_data));
    
    data->soc_samples.cpu_chann = IOReportCopyChannelsInGroup(CFSTR("CPU Stats"), 0, 0, 0, 0);
    data->soc_samples.cpu_sub  = IOReportCreateSubscription(NULL, data->soc_samples.cpu_chann, &data->soc_samples.cpu_sub_chann, 0, 0);
    
    CFRelease(data->soc_samples.cpu_chann);
}

static void sample(unit_data* unit_data) {
    CFDictionaryRef cpusamp_a  = IOReportCreateSamples(unit_data->soc_samples.cpu_sub, unit_data->soc_samples.cpu_sub_chann, NULL);
    CFDictionaryRef cpusamp_b  = IOReportCreateSamples(unit_data->soc_samples.cpu_sub, unit_data->soc_samples.cpu_sub_chann, NULL);
    CFDictionaryRef cpu_delta  = IOReportCreateSamplesDelta(cpusamp_a, cpusamp_b, NULL);
    
    // Done with these
    CFRelease(cpusamp_a);
    CFRelease(cpusamp_b);

    IOReportIterate(cpu_delta, ^int(IOReportSampleRef sample) {
        for (int i = 0; i < IOReportStateGetCount(sample); i++) {
            CFStringRef subgroup    = IOReportChannelGetSubGroup(sample);
            CFStringRef idx_name    = IOReportStateGetNameForIndex(sample, i);
            CFStringRef chann_name  = IOReportChannelGetChannelName(sample);
            uint64_t  residency   = IOReportStateGetResidency(sample, i);
            //CFShow(idx_name);
            // Ecore and Pcore
            int n_cores_hc = 2;
            const void *complex_chann_keys_hc[] = {CFSTR("ECPU"), CFSTR("PCPU"), CFSTR("GPUPH")};
            CFArrayRef complex_chann_keys = CFArrayCreate(kCFAllocatorDefault, complex_chann_keys_hc, 3, &kCFTypeArrayCallBacks);

            for (int ii = 0; ii < n_cores_hc + 1; ii++) {
                if (CFStringCompare(subgroup, CFSTR("CPU Complex Performance States"), 0) == kCFCompareEqualTo ||
                    CFStringCompare(subgroup, CFSTR("GPU Performance States"), 0) == kCFCompareEqualTo) {
                    
                    // Make sure channel name is correct
                    if (CFStringCompare(chann_name, (CFStringRef)CFArrayGetValueAtIndex(complex_chann_keys, ii),0) != kCFCompareEqualTo) continue;
                    // Make sure there is an active residency
                    if (CFStringFind(idx_name, ptype_state, 0).location != kCFNotFound || 
                        CFStringFind(idx_name, vtype_state, 0).location != kCFNotFound){
                        CFShow(idx_name);
                    }
                }
                else if (CFStringCompare(subgroup, CFSTR("CPU Core Performance States"), 0) == kCFCompareEqualTo) {
                    //CFShow(subgroup);
                    continue;
                }
            }
        }
        return kIOReportIterOk;
    });
    CFRelease(cpu_delta);
}

int main(int argc, char* argv[]) {
    unit_data* unit = malloc(sizeof(unit_data));

    // initialize the cmd_data
    init_unit_data(unit);
    sample(unit);
}

