/* Barcode decoding: turn measured black/white durations into a validated
 * one-character Code 39 result, including forward/reverse scan direction. */
#include "barcode_decoder.h"
#include <stddef.h>

#define BC_STAR 0x094U
#define BC_QUIET_MODULES 10U
#define BC_MIN_QUIET_US (S3_BC_MIN_NARROW_US * BC_QUIET_MODULES)
static const char s_alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";
/* Nine width bits in scan order: 1=wide, 0=narrow, starting with black.
 * Reference: standard Code 39; cross-checked against ReportLab's encoder. */
static const uint16_t s_patterns[] = {
    0x034, 0x121, 0x061, 0x160, 0x031, 0x130, 0x070, 0x025, 0x124, 0x064,
    0x109, 0x049, 0x148, 0x019, 0x118, 0x058, 0x00d, 0x10c, 0x04c, 0x01c,
    0x103, 0x043, 0x142, 0x013, 0x112, 0x052, 0x007, 0x106, 0x046, 0x016,
    0x181, 0x0c1, 0x1c0, 0x091, 0x190, 0x0d0, 0x085, 0x184, 0x0c4, 0x0a8,
    0x0a2, 0x08a, 0x02a
};

char s3_barcode_character(uint8_t code)
{
    unsigned index = code & S3_BC_SYMBOL_MASK;
    if ((code & S3_BC_VALID) == 0U || index >= sizeof(s_patterns)/sizeof(s_patterns[0]))
        return '\0';
    return s_alphabet[index];
}

static unsigned reverse_nine(unsigned pattern)
{
    unsigned i, result = 0;
    for (i = 0; i < 9U; ++i) {
        result = (result << 1) | (pattern & 1U);
        pattern >>= 1;
    }
    return result;
}

/* A character has exactly six narrow and three wide elements. Validate
 * both clusters instead of blindly selecting the three largest values. */
static int classify(const uint32_t *widths, unsigned *pattern, uint32_t *narrow)
{
    uint32_t sorted[9], n = 0, w = 0;
    unsigned i, j;
    *pattern = 0;
    for (i = 0; i < 9U; ++i) {
        if (widths[i] == 0U || widths[i] > S3_BC_MAX_RUN_US) return 0;
        sorted[i] = widths[i];
        for (j = i; j > 0U && sorted[j] < sorted[j - 1U]; --j) {
            uint32_t temp = sorted[j];
            sorted[j] = sorted[j - 1U];
            sorted[j - 1U] = temp;
        }
    }
    for (i = 0; i < 6U; ++i) n += sorted[i];
    for (i = 6; i < 9U; ++i) w += sorted[i];
    n /= 6U;
    w /= 3U;
    /* Empirical sensor allowance: mean wide/narrow ratio 1.8 through 4.0. */
    if (n < S3_BC_MIN_NARROW_US || n > S3_BC_MAX_NARROW_US ||
        w * 10U < n * 18U || w * 10U > n * 40U) return 0;
    for (i = 0; i < 9U; ++i) {
        uint32_t mean = i < 6U ? n : w;
        /* Allow +/-40% around each cluster mean for hand-scan variation. */
        if (sorted[i] * 5U < mean * 3U || sorted[i] * 5U > mean * 7U) return 0;
    }
    /* A clear split is necessary even at the tolerance boundaries. */
    if (sorted[6] * 10U < sorted[5] * 14U) return 0;
    for (i = 0; i < 9U; ++i)
        *pattern = (*pattern << 1) | (widths[i] >= sorted[6] ? 1U : 0U);
    *narrow = n;
    return 1;
}

/* Complete-frame validation: check widths, gaps, white margins, start/stop
 * guards and the data character before returning a packed result. */
int s3_barcode_decode(const uint32_t widths[S3_BC_RUNS],
                      uint32_t leading_white_us, uint32_t trailing_white_us,
                      uint8_t *code)
{
    unsigned patterns[3], i, reversed;
    uint32_t narrow[3], smallest, largest;
    if (code == NULL) return S3_BC_INVALID;
    *code = 0;
    if (widths == NULL) return S3_BC_INVALID;
    for (i = 0; i < 3U; ++i)
        if (!classify(widths + i * 10U, &patterns[i], &narrow[i])) return S3_BC_INVALID;
    smallest = largest = narrow[0];
    for (i = 1; i < 3U; ++i) {
        if (narrow[i] < smallest) smallest = narrow[i];
        if (narrow[i] > largest) largest = narrow[i];
    }
    /* Reject severe acceleration/distortion; speed may vary between scans. */
    if (largest * 10U > smallest * 14U) return S3_BC_INVALID;
    for (i = 0; i < 2U; ++i) {
        const uint32_t gap = widths[9U + i * 10U];
        /* Use the neighboring characters symmetrically when speed changes. */
        const uint32_t reference = (narrow[i] + narrow[i + 1U]) / 2U;
        if (gap < (reference + 1U) / 2U || gap > reference * 18U / 10U)
            return S3_BC_INVALID;
    }
    if (leading_white_us < narrow[0] * BC_QUIET_MODULES ||
        trailing_white_us < narrow[2] * BC_QUIET_MODULES)
        return S3_BC_INVALID;
    if (patterns[0] == BC_STAR && patterns[2] == BC_STAR) reversed = 0;
    else if (reverse_nine(patterns[0]) == BC_STAR &&
             reverse_nine(patterns[2]) == BC_STAR) reversed = S3_BC_REVERSED;
    else return S3_BC_INVALID;
    if (reversed) patterns[1] = reverse_nine(patterns[1]);
    for (i = 0; i < sizeof(s_patterns)/sizeof(s_patterns[0]); ++i) {
        if (patterns[1] == s_patterns[i]) {
            *code = (uint8_t)(S3_BC_VALID | reversed | i);
            return S3_BC_OK;
        }
    }
    return S3_BC_INVALID;
}

/* A score of 256 is one typical wide-minus-narrow width of error.
 * Use the same units for ink and paper and bound the impact of one outlier. */
#define BC_SCORE_ONE         256U
#define BC_GUARD_MAX_COST    640U
#define BC_DATA_MAX_COST     736U
#define BC_MIN_SCORE_MARGIN   77U

typedef struct {
    uint32_t best, second, data_cost, quiet;
    uint8_t code;
} ANALOG_MATCH;

static void insert_width(uint32_t *sorted, unsigned count, uint32_t width)
{
    while(count>0U && sorted[count-1U]>width){
        sorted[count]=sorted[count-1U];
        --count;
    }
    sorted[count]=width;
}

static unsigned width_class(unsigned pattern, unsigned index)
{
    return 2U*(index&1U)+((pattern>>(8U-index))&1U);
}

static uint32_t narrow_median(const uint32_t *widths, unsigned pattern)
{
    uint32_t narrow[6];
    unsigned count=0;
    for(unsigned i=0;i<9U;++i)
        if(!(width_class(pattern,i)&1U))insert_width(narrow,count++,widths[i]);
    return (narrow[2]+narrow[3])/2U;
}

/* The two known guards supply four typical widths. Using medians keeps
 * a single stretched run from becoming the reference for every letter. */
static int guard_profile(const uint32_t *widths, unsigned guard, uint32_t profile[4])
{
    uint32_t samples[4][6];
    unsigned counts[4]={0,0,0,0};
    for(unsigned i=0;i<9U;++i){
        const unsigned group=width_class(guard,i);
        insert_width(samples[group],counts[group]++,widths[i]);
        insert_width(samples[group],counts[group]++,widths[20U+i]);
    }
    for(unsigned i=0;i<4U;++i){
        const unsigned n=counts[i];
        profile[i]=(samples[i][(n-1U)/2U]+samples[i][n/2U])/2U;
    }
    return profile[0]>=S3_BC_MIN_NARROW_US && profile[2]>=S3_BC_MIN_NARROW_US &&
           profile[1]*2U>=profile[0]*3U && profile[3]*2U>=profile[2]*3U;
}

static uint32_t symbol_cost(const uint32_t *widths, unsigned pattern,
                            const uint32_t profile[4], uint32_t *scale)
{
    uint32_t ratios[9], cost=0;
    for(unsigned i=0;i<9U;++i){
        const unsigned group=width_class(pattern,i);
        insert_width(ratios,i,widths[i]*BC_SCORE_ONE/profile[group]);
    }
    *scale=ratios[4];
    if(*scale<BC_SCORE_ONE/2U || *scale>BC_SCORE_ONE*2U)return UINT32_MAX;
    for(unsigned i=0;i<9U;++i){
        const unsigned group=width_class(pattern,i);
        const uint32_t expected=profile[group]*(*scale)/BC_SCORE_ONE;
        const uint32_t span=(profile[group|1U]-profile[group&~1U])*(*scale)/BC_SCORE_ONE;
        const uint32_t error=expected>widths[i]?expected-widths[i]:widths[i]-expected;
        const uint32_t penalty=error*BC_SCORE_ONE/span;
        cost+=penalty<BC_SCORE_ONE*3U/2U?penalty:BC_SCORE_ONE*3U/2U;
    }
    return cost;
}

static uint32_t analog_space_unit(const uint32_t *widths,unsigned pattern)
{
    uint32_t sum=0;
    unsigned spaces=0;
    for(unsigned i=1U;i<9U;i+=2U){
        if(pattern&(1U<<(8U-i)))continue;
        sum+=widths[i];
        ++spaces;
    }
    /* Four white elements with only three wide elements in a symbol. */
    return sum/spaces;
}

static int analog_gap(uint32_t gap, uint32_t before, uint32_t after)
{
    const uint32_t unit=(before+after)/2U;
    return gap>=(unit+1U)/2U && gap<=unit*18U/10U;
}

static void match_analog_frame(const uint32_t *widths, uint32_t leading, uint32_t pulse,
                               ANALOG_MATCH *match)
{
    for(unsigned i=0;i<S3_BC_RUNS;++i)
        if(!widths[i] || widths[i]>S3_BC_MAX_RUN_US)return;
    for(unsigned reversed=0;reversed<2U;++reversed){
        const unsigned guard=reversed?reverse_nine(BC_STAR):BC_STAR;
        const uint32_t start=narrow_median(widths,guard);
        const uint32_t end=narrow_median(widths+20U,guard);
        uint32_t profile[4], start_scale, end_scale;
        if(leading<start*BC_QUIET_MODULES ||
           !guard_profile(widths,guard,profile))continue;
        const uint32_t start_cost=symbol_cost(widths,guard,profile,&start_scale);
        const uint32_t end_cost=symbol_cost(widths+20U,guard,profile,&end_scale);
        if(start_cost>BC_GUARD_MAX_COST || end_cost>BC_GUARD_MAX_COST ||
           pulse*4U>(start<end?start:end))continue;
        const uint32_t start_space=analog_space_unit(widths,guard);
        const uint32_t end_space=analog_space_unit(widths+20U,guard);
        const uint32_t small_scale=start_scale<end_scale?start_scale:end_scale;
        const uint32_t large_scale=start_scale>end_scale?start_scale:end_scale;
        for(unsigned i=0;i<sizeof(s_patterns)/sizeof(s_patterns[0]);++i){
            const unsigned pattern=reversed?reverse_nine(s_patterns[i]):s_patterns[i];
            uint32_t scale;
            const uint32_t data_cost=symbol_cost(widths+10U,pattern,profile,&scale);
            if(data_cost==UINT32_MAX ||
               (scale>large_scale?scale:large_scale)*3U>
               (scale<small_scale?scale:small_scale)*5U)continue;
            const uint32_t middle_space=analog_space_unit(widths+10U,pattern);
            if(!analog_gap(widths[9],start_space,middle_space) ||
               !analog_gap(widths[19],middle_space,end_space))continue;
            const uint32_t cost=start_cost+data_cost+end_cost;
            const uint8_t code=(uint8_t)(S3_BC_VALID|(reversed?S3_BC_REVERSED:0U)|i);
            if(cost<match->best){
                if(code!=match->code)match->second=match->best;
                match->best=cost;
                match->data_cost=data_cost;
                match->code=code;
                match->quiet=end*BC_QUIET_MODULES;
            }else if(code!=match->code && cost<match->second){
                match->second=cost;
            }
        }
    }
}

int s3_barcode_decode_analog(const uint32_t *widths, unsigned count,
                            uint32_t leading_white_us, uint32_t *quiet_us,
                            uint8_t *code)
{
    ANALOG_MATCH match={.best=UINT32_MAX,.second=UINT32_MAX};
    if(!code || !quiet_us)return S3_BC_INVALID;
    *code=0;
    *quiet_us=0;
    if(!widths)return S3_BC_INVALID;
    if(count==S3_BC_RUNS){
        match_analog_frame(widths,leading_white_us,0U,&match);
    }else if(count==S3_BC_RUNS+2U){
        uint32_t corrected[S3_BC_RUNS], minimum=UINT32_MAX;
        unsigned corrections=0;
        for(unsigned i=0;i<count;++i){
            if(widths[i]==0U || widths[i]>S3_BC_MAX_RUN_US)return S3_BC_INVALID;
            if(widths[i]<minimum)minimum=widths[i];
        }
        for(unsigned i=1U;i+1U<count;++i){
            /* One brief opposite-color excursion divides a genuine run.
             * Require its neighbors together to be much longer. */
            if(widths[i]>minimum*2U || widths[i]*8U>widths[i-1U]+widths[i+1U])continue;
            if(++corrections>4U)return S3_BC_INVALID; /* Bound work on noisy input. */
            for(unsigned j=0;j<i-1U;++j)corrected[j]=widths[j];
            corrected[i-1U]=widths[i-1U]+widths[i]+widths[i+1U];
            for(unsigned j=i;j<S3_BC_RUNS;++j)corrected[j]=widths[j+2U];
            match_analog_frame(corrected,leading_white_us,widths[i],&match);
        }
    }
    if(!match.code || match.data_cost>BC_DATA_MAX_COST ||
       match.second<match.best+BC_MIN_SCORE_MARGIN)return S3_BC_INVALID;
    *quiet_us=match.quiet;
    *code=match.code;
    return S3_BC_OK;
}

/* Streaming decoder: reset, poll and edge() collect a frame over successive
 * calls, enforce timeouts, and wait for trailing white before reporting. */
void s3_barcode_reset(S3_BARCODE_DECODER *decoder, int black, uint32_t now_us)
{
    if (decoder == NULL) return;
    decoder->count = 0;
    decoder->black = black != 0;
    decoder->capturing = 0;
    decoder->pending = 0;
    decoder->pending_code = 0;
    decoder->last_edge_us = now_us;
    decoder->frame_start_us = now_us;
    decoder->leading_white_us = 0;
    decoder->quiet_needed_us = 0;
}

int s3_barcode_poll(S3_BARCODE_DECODER *decoder, uint32_t now_us, uint8_t *code)
{
    uint32_t elapsed;
    if (decoder == NULL || code == NULL) return S3_BC_INVALID;
    *code = 0;
    elapsed = now_us - decoder->last_edge_us;
    if (decoder->pending && elapsed >= decoder->quiet_needed_us) {
        *code = decoder->pending_code;
        decoder->pending = 0;
        return S3_BC_OK;
    }
    if (decoder->capturing && (elapsed > S3_BC_MAX_RUN_US ||
        now_us - decoder->frame_start_us > S3_BC_FRAME_TIMEOUT_US)) {
        decoder->capturing = 0;
        decoder->count = 0;
        return S3_BC_TIMEOUT;
    }
    /* Cap a long idle white interval, so a 71-minute timer wrap cannot
     * make an already established quiet zone appear too short. */
    if (!decoder->capturing && !decoder->pending && !decoder->black &&
        elapsed > S3_BC_QUIET_CAP_US)
        decoder->last_edge_us = now_us - S3_BC_QUIET_CAP_US;
    return S3_BC_NONE;
}

int s3_barcode_edge(S3_BARCODE_DECODER *decoder, int black, uint32_t now_us,
                    uint8_t *code)
{
    uint32_t duration;
    int result;
    if (decoder == NULL || code == NULL) return S3_BC_INVALID;
    *code = 0;
    if ((black != 0) == decoder->black) return S3_BC_NONE;
    result = s3_barcode_poll(decoder, now_us, code);
    duration = now_us - decoder->last_edge_us;
    if (decoder->pending) {
        /* More ink before the required trailing white: not this frame. */
        decoder->pending = 0;
        result = S3_BC_INVALID;
    }
    if (decoder->capturing && black && duration >= BC_MIN_QUIET_US &&
        duration >= decoder->quiet_needed_us) {
        /* This white run is too long to belong to the partial label. The
         * same edge may start another label after this quiet interval. */
        decoder->capturing = 0;
        result = S3_BC_INVALID;
    }
    if (decoder->capturing) {
        decoder->widths[decoder->count++] = duration;
        if (decoder->count == 1U) {
            /* Both start-guard orientations start with a narrow black bar.
             * Even at the accepted 4:1 ratio and +/-40% spread, a guard
             * element is shorter than ten times that first bar. */
            decoder->quiet_needed_us = duration * BC_QUIET_MODULES;
        } else if (decoder->count == 9U) {
            unsigned pattern;
            uint32_t narrow;
            if (!classify(decoder->widths, &pattern, &narrow) ||
                (pattern != BC_STAR && reverse_nine(pattern) != BC_STAR) ||
                decoder->leading_white_us < narrow * BC_QUIET_MODULES) {
                decoder->capturing = 0;
                result = S3_BC_INVALID;
            } else {
                /* Remaining character means may be at most 1.4x this
                 * guard mean. Their widest accepted run is still <10x. */
                decoder->quiet_needed_us = narrow * BC_QUIET_MODULES;
            }
        } else if (decoder->count == S3_BC_RUNS) {
            unsigned pattern;
            uint32_t narrow;
            decoder->capturing = 0;
            result = s3_barcode_decode(decoder->widths, decoder->leading_white_us,
                                       UINT32_MAX, &decoder->pending_code);
            if (result == S3_BC_OK && classify(decoder->widths + 20U, &pattern, &narrow)) {
                decoder->quiet_needed_us = narrow * BC_QUIET_MODULES;
                decoder->pending = 1;
                result = S3_BC_NONE; /* wait for the real trailing white */
            }
        }
    } else if (black && duration >= BC_MIN_QUIET_US) {
        decoder->count = 0;
        decoder->capturing = 1;
        decoder->frame_start_us = now_us;
        decoder->leading_white_us = duration;
    }
    decoder->black = black != 0;
    decoder->last_edge_us = now_us;
    return result;
}
