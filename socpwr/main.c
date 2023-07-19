
#import "main.h"
/*
    Notes:

    make sure data collection knows when it failed:
        - scheduled on core it wasn't supposed to run
        - or switch cores
    if this happens throw away that run

    is there a way to see what core you are running on?

    In hertzbleed, we want to sample on either end of some hertzbleed code THEN compute delta
    For HB Port:
    - Do leakage model 03, HD or HW (shifts value over and back)
        - Replace all freq and pwr 
        - port assembly to arm
    - what we want is instantaneous frequency measurement
    - Seperate monitor thread that sits there and does short sleeps then calculates freq
    - For assembly:
        - Designed to saturate system -> use every register as possible
            - Need to see what registers are available to us
            - Pick the same number as what they did in hertzbleed
*/
/* data structs
*/
typedef struct cpu_samples_perf_data{
    CFMutableArrayRef sums; /* sum of state distribution ticks per-cluster */
    CFMutableArrayRef distribution; /* distribution[CLUSTER][STATE]: distribution of individual states */
    //CFMutableArrayRef freqs; /* calculated "active" frequency per-cluster */
    CFMutableArrayRef residency; /* calculated "active" usage/residency per-cluster */
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

// Hard coded values
static int n_cores_hc = 2;
static int per_cluster_n_cores = 4;
static float array1[] = {600, 912, 1284, 1752, 2004, 2256, 2424};
static float array2[] = {660, 924, 1188, 1452, 1704, 1968, 2208, 2400, 2568, 2724, 2868, 2988, 3096, 3204, 3324, 3408, 3504};
static float array3[] = {444, 612, 808, 968, 1110, 1236, 1338, 1398};

static float* freq_state_cores[] = {array1, array2, array3};

static CFStringRef ptype_state = CFSTR("P");
static CFStringRef vtype_state = CFSTR("V");
static CFStringRef idletype_state = CFSTR("IDLE");
static CFStringRef downtype_state = CFSTR("DOWN");
static CFStringRef offtype_state = CFSTR("OFF");

/* defs
 */
static inline void init_unit_data(unit_data *data);
static void sample(unit_data *unit_data);
static void format(unit_data* unit_data);

static inline void init_unit_data(unit_data *data) {
    memset((void*)data, 0, sizeof(unit_data));
    
    cpu_samples_perf_data *cluster_perf_data = &data->soc_samples.cluster_perf_data;
    cpu_samples_perf_data *core_perf_data = &data->soc_samples.core_perf_data;

    cluster_perf_data->sums = CFArrayCreateMutable(kCFAllocatorDefault, 3, &kCFTypeArrayCallBacks);
    cluster_perf_data->distribution = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    cluster_perf_data->residency =  CFArrayCreateMutable(kCFAllocatorDefault, 3, &kCFTypeArrayCallBacks);

    core_perf_data->sums = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    core_perf_data->distribution = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    core_perf_data->residency =  CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    
    /* init for each cluster and core */
    for (int i = 0; i < n_cores_hc + 1; i++) {
        CFArrayAppendValue(cluster_perf_data->distribution, CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
        CFArrayAppendValue(cluster_perf_data->residency, CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &(uint64_t){0}));
        CFArrayAppendValue(cluster_perf_data->sums, CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &(uint64_t){0}));

        if (i < n_cores_hc){
            CFMutableArrayRef dist_arr = CFArrayCreateMutable(kCFAllocatorDefault, per_cluster_n_cores, &kCFTypeArrayCallBacks);
            CFMutableArrayRef residency_arr = CFArrayCreateMutable(kCFAllocatorDefault, per_cluster_n_cores, &kCFTypeArrayCallBacks);
            CFMutableArrayRef sums_arr = CFArrayCreateMutable(kCFAllocatorDefault, per_cluster_n_cores, &kCFTypeArrayCallBacks);
    
            for (int ii = 0; ii < per_cluster_n_cores; ii++) {
                CFNumberRef zero = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, (const void*)&(int){0});
                CFArrayAppendValue(dist_arr, CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
                CFArrayAppendValue(residency_arr, zero);
                CFArrayAppendValue(sums_arr, zero);
                CFRelease(zero);
            }

            CFArraySetValueAtIndex(core_perf_data->distribution, i, dist_arr);
            CFArraySetValueAtIndex(core_perf_data->residency, i, residency_arr);
            CFArraySetValueAtIndex(core_perf_data->sums, i, sums_arr);
            CFRelease(dist_arr);
            CFRelease(residency_arr);
            CFRelease(sums_arr);
        }
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
    CFArrayRef complex_freq_chankeys = CFArrayCreate(kCFAllocatorDefault, (const void *[]){CFSTR("ECPU"), CFSTR("PCPU"), CFSTR("GPUPH")}, 3, &kCFTypeArrayCallBacks);
    CFArrayRef core_freq_chankeys = CFArrayCreate(kCFAllocatorDefault, (const void *[]){CFSTR("ECPU"), CFSTR("PCPU")}, 2, &kCFTypeArrayCallBacks);
    
    CFDictionaryRef cpusamp_a  = IOReportCreateSamples(unit_data->soc_samples.cpu_sub, unit_data->soc_samples.cpu_sub_chann, NULL);
    usleep(275 * 1e3);
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
            uint64_t residency   = IOReportStateGetResidency(sample, i);
            
            // Ecore and Pcore
            for (int ii = 0; ii < n_cores_hc + 1; ii++) {
                if (CFStringCompare(subgroup, CFSTR("CPU Complex Performance States"), 0) == kCFCompareEqualTo ||
                    CFStringCompare(subgroup, CFSTR("GPU Performance States"), 0) == kCFCompareEqualTo) {
                    
                    if (CFStringCompare(chann_name, (CFStringRef)CFArrayGetValueAtIndex(complex_freq_chankeys, ii),0) != kCFCompareEqualTo) continue;
            
                    // Make sure there is an active residency
                    if (CFStringFind(idx_name, ptype_state, 0).location != kCFNotFound || 
                        CFStringFind(idx_name, vtype_state, 0).location != kCFNotFound){
                        //printf("%llu\n", residency);
                        // Sum all for complex
                        uint64_t sum;
                        CFNumberRef old_sum = (CFNumberRef)CFArrayGetValueAtIndex(unit_data->soc_samples.cluster_perf_data.sums, ii);
                        CFNumberGetValue(old_sum, kCFNumberSInt64Type, &sum);
                        uint64_t new_sum = sum + residency;
                        CFArraySetValueAtIndex(unit_data->soc_samples.cluster_perf_data.sums, ii, CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &new_sum));

                        CFMutableArrayRef comp_distribution = (CFMutableArrayRef)CFArrayGetValueAtIndex(unit_data->soc_samples.cluster_perf_data.distribution, ii);
                        CFNumberRef distribution = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &residency);
                        CFArrayAppendValue(comp_distribution, distribution);
                        
                        sum = 0;
                        CFRelease(distribution);
                    }
                    else if (CFStringFind(idx_name, idletype_state, 0).location != kCFNotFound || 
                            CFStringFind(idx_name, offtype_state, 0).location != kCFNotFound){
                        CFArraySetValueAtIndex(unit_data->soc_samples.cluster_perf_data.residency, ii, CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &residency));
                    }
                }
                else if (CFStringCompare(subgroup, CFSTR("CPU Core Performance States"), 0) == kCFCompareEqualTo && ii < n_cores_hc) {
                    for (int iii = 0; iii < per_cluster_n_cores; iii++) {
                        CFStringRef key = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("%@%d"), (CFStringRef)CFArrayGetValueAtIndex(complex_freq_chankeys, ii), iii);
                        
                        if (CFStringCompare(chann_name, key, 0) != kCFCompareEqualTo){
                            CFRelease(key);
                            continue;
                        }
                        // active residency
                        if (CFStringFind(idx_name, ptype_state, 0).location != kCFNotFound ||
                            CFStringFind(idx_name, vtype_state, 0).location != kCFNotFound){
                            CFMutableArrayRef sum_array = (CFMutableArrayRef)CFArrayGetValueAtIndex(unit_data->soc_samples.core_perf_data.sums, ii);
                            CFMutableArrayRef distribution_array = (CFMutableArrayRef)CFArrayGetValueAtIndex(unit_data->soc_samples.core_perf_data.distribution, ii);
                            
                            // Sum
                            uint64_t sum;
                            CFNumberRef old_sum = (CFNumberRef)CFArrayGetValueAtIndex(sum_array, iii);
                            CFNumberGetValue(old_sum, kCFNumberSInt64Type, &sum);
                            uint64_t new_sum = sum + residency;
                            CFArraySetValueAtIndex(sum_array, iii, CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &new_sum));
                            // Dist
                            CFMutableArrayRef comp_distribution = (CFMutableArrayRef)CFArrayGetValueAtIndex(distribution_array, iii);
                            CFNumberRef distribution = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &residency);
                            CFArrayAppendValue(comp_distribution, distribution);
                           
                            sum = 0;
                            CFRelease(distribution);
                        }
                        else if (CFStringFind(idx_name, idletype_state, 0).location != kCFNotFound || 
                            CFStringFind(idx_name, offtype_state, 0).location != kCFNotFound){
                            CFMutableArrayRef residency_array = (CFMutableArrayRef)CFArrayGetValueAtIndex(unit_data->soc_samples.core_perf_data.residency, ii);
                            CFArraySetValueAtIndex(residency_array, iii, CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &residency));   
                        }

                    }
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

static void format(unit_data* unit_data) {
    uint64_t res = 0;
    uint64_t core_res = 0;
    float complex_freqs[3] = {0,0,0};
    float core_freqs[2][4] = {{0,0,0,0},
                              {0,0,0,0}};

    for (int i = 0; i < n_cores_hc + 1; i++) {
        
        // Add all frequency values
        CFArrayRef complex_dists = CFArrayGetValueAtIndex(unit_data->soc_samples.cluster_perf_data.distribution, i);
        for (int ii = 0; ii < CFArrayGetCount(complex_dists); ii++) {
            CFNumberRef value = CFArrayGetValueAtIndex(complex_dists, ii);
            CFNumberGetValue(value, kCFNumberSInt64Type, &res);
            
            if (res != 0){
                float sum;
                CFNumberRef sum_num = CFArrayGetValueAtIndex(unit_data->soc_samples.cluster_perf_data.sums, i);
                CFNumberGetValue(sum_num, kCFNumberFloatType, &sum);
                float percent = (res / sum);
                complex_freqs[i] = complex_freqs[i] + (freq_state_cores[i][ii] * percent);
                // The numbers here for i = 0 and i = 1 are the complex freq values
                // printf("%d: %f\n", i, complex_freq);
            }

            if (i < n_cores_hc){
                CFArrayRef core_dists = CFArrayGetValueAtIndex(unit_data->soc_samples.core_perf_data.distribution, i);
                
                for (int iii = 0; iii < per_cluster_n_cores; iii++) {
                   
                    
                    CFArrayRef core_dists_arr = CFArrayGetValueAtIndex(core_dists, iii);
                    CFNumberRef core_value = CFArrayGetValueAtIndex(core_dists_arr, ii);
                    CFNumberGetValue(core_value, kCFNumberSInt64Type, &core_res);
                    if (core_res != 0) {
                        // printf("i: %d\n", i);
                        // printf("ii: %d\n", ii);
                        // printf("iii: %d\n", iii);
                        // printf("==\n");
                        float sum;
                        CFArrayRef sums = CFArrayGetValueAtIndex(unit_data->soc_samples.core_perf_data.sums, i);
                        CFNumberRef sum_num = CFArrayGetValueAtIndex(sums, iii);
                        CFNumberGetValue(sum_num, kCFNumberFloatType, &sum);
                        float core_percent = (core_res / sum);
                        core_freqs[i][iii] = core_freqs[i][iii] + (freq_state_cores[i][ii] * core_percent);
                    }
                }
            }        
       }
    }
}

int main(int argc, char* argv[]) {
    unit_data* unit = malloc(sizeof(unit_data));

    // initialize the cmd_data
    init_unit_data(unit);
    sample(unit);
    format(unit);


}
    

