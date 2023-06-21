
#import "main.h"

/* data structs
*/
typedef struct unit_data {
    struct {
        IOReportSubscriptionRef cpu_sub;
        CFMutableDictionaryRef cpu_sub_chann;
        CFMutableDictionaryRef cpu_chann;
    } soc_samples;
} unit_data;

/* defs
 */
static inline void init_unit_data(unit_data* data);
static void sample(unit_data* unit_data);

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
    
    IOReportIterate(cpu_delta, ^int(IOReportSampleRef sample) {
        for (int i = 0; i < IOReportStateGetCount(sample); i++) {
            CFShow(sample);
        }
        return kIOReportIterOk;
    });
    CFRelease(cpu_delta);
}

int main(int argc, char * argv[]) {
    unit_data* unit = malloc(sizeof(unit_data));

    // initialize the cmd_data
    init_unit_data(unit);
    sample(unit);
}

