#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "beacon.h"
#include "ax25.h"
#include "kiss.h"

int main(void) {
    beacon_state_t state;
    int rc = beacon_init(&state, "G4DPZ-1", 52.467, -2.022, "test", 120);
    printf("beacon_init returned %d\n", rc);
    printf("kiss_frame_len = %d\n", state.kiss_frame_len);
    
    printf("KISS frame bytes: ");
    for (int i = 0; i < state.kiss_frame_len; i++)
        printf("%02X ", state.kiss_frame[i]);
    printf("\n");
    
    /* KISS decode */
    kiss_decoder_t dec;
    kiss_decoder_init(&dec);
    uint8_t ax25_buf[512];
    size_t ax25_len = 0;
    int got_frame = 0;
    for (int i = 0; i < state.kiss_frame_len; i++) {
        int r = kiss_decoder_feed(&dec, state.kiss_frame[i],
                                  ax25_buf, sizeof(ax25_buf), &ax25_len);
        if (r == 1) { got_frame = 1; break; }
    }
    printf("got_frame=%d, ax25_len=%zu\n", got_frame, ax25_len);
    
    if (got_frame) {
        printf("AX.25 bytes: ");
        for (size_t i = 0; i < ax25_len; i++)
            printf("%02X ", ax25_buf[i]);
        printf("\n");
        
        char src[12], dst[12];
        const uint8_t *info;
        int info_len = ax25_strip_frame(ax25_buf, ax25_len, src, dst, &info);
        printf("info_len=%d, src='%s', dst='%s'\n", info_len, src, dst);
    }
    
    /* Now test with a short callsign like "A" */
    rc = beacon_init(&state, "A", 52.0, -2.0, "test", 120);
    printf("\nbeacon_init('A') returned %d\n", rc);
    if (rc == 0) {
        printf("kiss_frame_len = %d\n", state.kiss_frame_len);
        
        kiss_decoder_init(&dec);
        ax25_len = 0;
        got_frame = 0;
        for (int i = 0; i < state.kiss_frame_len; i++) {
            int r = kiss_decoder_feed(&dec, state.kiss_frame[i],
                                      ax25_buf, sizeof(ax25_buf), &ax25_len);
            if (r == 1) { got_frame = 1; break; }
        }
        printf("got_frame=%d, ax25_len=%zu\n", got_frame, ax25_len);
        
        if (got_frame) {
            char src[12], dst[12];
            const uint8_t *info;
            int info_len = ax25_strip_frame(ax25_buf, ax25_len, src, dst, &info);
            printf("info_len=%d, src='%s', dst='%s'\n", info_len, src, dst);
        }
    }
    
    return 0;
}
