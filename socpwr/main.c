
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
typedef struct cpu_samples_perf_data{
    uint64_t *sums; /* sum of state distribution ticks per-cluster */
    CFMutableArrayRef distribution; /* distribution[CLUSTER][STATE]: distribution of individual states */
    // CFMutableArrayRef *freqs; /* calculated "active" frequency per-cluster */
    // CFMutableArrayRef *volts; /* calculated "active" voltage per-cluster */
    // CFMutableArrayRef *residency; /* calculated "active" usage/residency per-cluster */
} cpu_samples_perf_data;

typedef struct unit_data {
    struct {
        IOReportSubscriptionRef cpu_sub;
        CFMutableDictionaryRef cpu_sub_chann;
        CFMutableDictionaryRef cpu_chann;
        CFMutableDictionaryRef gpu_chann;

        cpu_samples_perf_data cluster_perf_data;
        cpu_samples_perf_data core_perf_data;
    } soc_samples;
} unit_data;

static int n_cores_hc = 2;
static CFStringRef ptype_state = CFSTR("P");
static CFStringRef vtype_state = CFSTR("V");
static CFStringRef idletype_state = CFSTR("IDLE");
static CFStringRef downtype_state = CFSTR("DOWN");
static CFStringRef offtype_state = CFSTR("OFF");

/* defs
 */
static inline void init_unit_data(unit_data* data);
static void sample(unit_data* unit_data);
static inline void get_core_counts(unit_data* unit_data);

static inline void init_unit_data(unit_data* data) {
    memset((void*)data, 0, sizeof(unit_data));
    
    cpu_samples_perf_data* cluster_perf_data = &data->soc_samples.cluster_perf_data;
    cluster_perf_data->sums = calloc(3, sizeof(uint64_t));
    cluster_perf_data->distribution = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    
    /* init for each cluster and core */
    for (int i = 0; i < n_cores_hc + 1; i++) {
        CFArrayAppendValue(cluster_perf_data->distribution, CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
    }

    //Initialize channels
    data->soc_samples.cpu_chann = IOReportCopyChannelsInGroup(CFSTR("CPU Stats"), 0, 0, 0, 0);
    data->soc_samples.gpu_chann = IOReportCopyChannelsInGroup(CFSTR("GPU Stats"), 0, 0, 0, 0);
    
    // Merge GPU and CPU channel
    IOReportMergeChannels(data->soc_samples.cpu_chann, data->soc_samples.gpu_chann, NULL);
    
    // Create subscription
    data->soc_samples.cpu_sub  = IOReportCreateSubscription(NULL, data->soc_samples.cpu_chann, &data->soc_samples.cpu_sub_chann, 0, 0);
    
    CFRelease(data->soc_samples.cpu_chann);
    CFRelease(data->soc_samples.gpu_chann);
}

static void sample(unit_data* unit_data) {
    CFDictionaryRef cpusamp_a  = IOReportCreateSamples(unit_data->soc_samples.cpu_sub, unit_data->soc_samples.cpu_sub_chann, NULL);
    CFDictionaryRef cpusamp_b  = IOReportCreateSamples(unit_data->soc_samples.cpu_sub, unit_data->soc_samples.cpu_sub_chann, NULL);
    usleep(275 * 1e-3);
   
    CFDictionaryRef cpu_delta  = IOReportCreateSamplesDelta(cpusamp_a, cpusamp_b, NULL);
    
    // Done with these
    CFRelease(cpusamp_a);
    CFRelease(cpusamp_b);

    IOReportIterate(cpu_delta, ^int(IOReportSampleRef sample) {
        for (int i = 0; i < IOReportStateGetCount(sample); i++) {
            CFStringRef subgroup    = IOReportChannelGetSubGroup(sample);
            CFStringRef idx_name    = IOReportStateGetNameForIndex(sample, i);
            CFStringRef chann_name  = IOReportChannelGetChannelName(sample);
            uint64_t residency   = IOReportStateGetResidency(sample, i);
            
            // Ecore and Pcore
            const void *complex_chann_keys_hc[] = {CFSTR("ECPU"), CFSTR("PCPU"), CFSTR("GPUPH")};
            CFArrayRef complex_chann_keys = CFArrayCreate(kCFAllocatorDefault, complex_chann_keys_hc, 3, &kCFTypeArrayCallBacks);
        
            for (int ii = 0; ii < n_cores_hc + 1; ii++) {
                if (CFStringCompare(subgroup, CFSTR("CPU Complex Performance States"), 0) == kCFCompareEqualTo ||
                    CFStringCompare(subgroup, CFSTR("GPU Performance States"), 0) == kCFCompareEqualTo) {
                    
                    if (CFStringCompare(chann_name, (CFStringRef)CFArrayGetValueAtIndex(complex_chann_keys, ii),0) != kCFCompareEqualTo) continue;
            
                    // Make sure there is an active residency
                    if (CFStringFind(idx_name, ptype_state, 0).location != kCFNotFound || 
                        CFStringFind(idx_name, vtype_state, 0).location != kCFNotFound){
                        
                        // Sum all for complex
                        uint64_t sum = unit_data->soc_samples.cluster_perf_data.sums[ii] + residency;
                        CFMutableArrayRef comp_distribution = (CFMutableArrayRef)CFArrayGetValueAtIndex(unit_data->soc_samples.cluster_perf_data.distribution, ii);
                        CFNumberRef distribution = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &residency);
                        CFArrayAppendValue(comp_distribution, distribution);
                        unit_data->soc_samples.cluster_perf_data.sums[ii] = sum + residency;
                        
                        sum = 0;
                        CFRelease(distribution);
                    }
                    else if (CFStringFind(idx_name, idletype_state, 0).location != kCFNotFound || 
                            CFStringFind(idx_name, offtype_state, 0).location != kCFNotFound){
                            
                    }
                }
                else if (CFStringCompare(subgroup, CFSTR("CPU Core Performance States"), 0) == kCFCompareEqualTo &&
                        ii < n_cores_hc) {
                    // CFShow(chann_name);
                    // printf("%llu\n", residency);
                }
            }
            chann_name = NULL;
            subgroup = NULL;
            idx_name = NULL;
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
    for (int i = 0; i < 3; i++){
        printf("%llu\n", unit->soc_samples.cluster_perf_data.sums[i]);
    }
}

