/* Offline C -> browser protocol evidence. Emits no network traffic. */
#include <stdio.h>
#include "../../../app_program/subsystem1/s1_protocol.h"

static int emit(uint32_t sequence, unsigned mm, unsigned bits, uint32_t age)
{
    S1_TELEMETRY sample;
    S1_LINE_TELEMETRY line;
    unsigned char bytes[11];
    unsigned i;
    if (s1_make_telemetry(&sample, sequence, mm, mm != 0) != S1_CODEC_OK ||
        s1_make_line(&line, bits & 3U, bits != 0, 1000) != S1_CODEC_OK ||
        s1_pack_frame(&sample, &line, NULL, 1000 + age, 100, bytes, sizeof(bytes)) != 11)
        return 1;
    printf("{\"kind\":\"telemetry\",\"sequence\":%u,\"distance\":%u,\"line\":%u,\"hex\":\"",
           (unsigned)sequence, mm, age > 100 ? 0U : bits);
    for (i = 0; i < sizeof(bytes); ++i) printf("%02x", bytes[i]);
    puts("\"}");
    return 0;
}
static int emit_imu(int pitch, unsigned height, unsigned max, unsigned state,
                     unsigned flags, uint32_t age)
{
    S1_TELEMETRY sample;
    S1_LINE_TELEMETRY line;
    S1_IMU_TELEMETRY imu;
    unsigned char bytes[11];
    unsigned i;
    unsigned expected_flags = flags;
    int expected_pitch = flags & S1_IMU_PITCH_VALID ? pitch : -9999;
    if (s1_make_telemetry(&sample, 12, 451, 1) ||
        s1_make_line(&line, 2, 1, 1000 + age) ||
        s1_make_imu(&imu, pitch, height, max, state, flags, 1000, 1000) ||
        s1_pack_frame(&sample, &line, &imu, 1000 + age, 100, bytes, sizeof(bytes)) != 11)
        return 1;
    if (age > 100U) { expected_pitch = -9999; expected_flags &= ~S1_IMU_PITCH_VALID; }
    if (age > S1_IMU_MAX_AGE_MS) {
        state=7; height=max=0; expected_flags=0;
    }
    printf("{\"kind\":\"imu\",\"pitch\":%d,\"height\":%u,\"max\":%u,\"state\":%u,\"flags\":%u,\"hex\":\"",
        expected_pitch, height, max, state, expected_flags);
    for(i=0;i<sizeof(bytes);++i) printf("%02x",bytes[i]);
    puts("\"}");
    return 0;
}
static int emit_status(void)
{
    const uint32_t ids[] = {1,12,0xffffffffU};
    const unsigned ages[] = {0,100,2000,S1_STATUS_NO_SAMPLE};
    S1_TELEMETRY sample;
    uint8_t request[5],frame[S1_FRAME_BYTES],reply[S1_STATUS_MAX_BYTES];
    unsigned i,j,mode,k;int length;uint32_t parsed;
    (void)s1_make_telemetry(&sample,12,451,1);
    (void)s1_pack_frame(&sample,NULL,NULL,1000,100,frame,sizeof(frame));
    for(i=0;i<3;++i) {
        request[0]=0xa0;s1_write_u32(request+1,ids[i]);
        if(s1_parse_status_request(request,5,&parsed)||parsed!=ids[i])return 4;
        printf("{\"kind\":\"status_request\",\"id\":%u,\"hex\":\"",(unsigned)ids[i]);
        for(k=0;k<5;++k)printf("%02x",request[k]);
        puts("\"}");
        for(j=0;j<4;++j)for(mode=0;mode<2;++mode) {
            length=s1_pack_status_reply(ids[i],ids[i],ages[j],mode,
                ages[j]==S1_STATUS_NO_SAMPLE?NULL:frame,reply,sizeof(reply));
            if(length<0)return 4;
            printf("{\"kind\":\"status\",\"id\":%u,\"age\":%u,\"test\":%u,\"hex\":\"",(unsigned)ids[i],ages[j],mode);
            for(k=0;k<(unsigned)length;++k)printf("%02x",reply[k]);
            puts("\"}");
        }
    }
    return 0;
}
int main(void)
{
    const unsigned states[] = {0, 4, 5, 6, 7};
    const uint32_t counters[] = {0, 12, 0xffffffffU};
    unsigned mm, j, k;
    unsigned char heartbeat[5];
    S1_TELEMETRY rejected;
    S1_IMU_TELEMETRY rejected_imu;
    int pitch;
    for (mm = 0; mm <= 4000; ++mm) {
        if (mm > 0 && mm < 20) continue;
        for (j = 0; j < 5; ++j)
            if (emit((uint32_t)(mm * 8U + j), mm, states[j], 0)) return 1;
    }
    if (emit(12, 451, 6, 0) || emit(0xffffffffU, 4000, 7, 0) ||
        emit(0x12345678U, 20, 5, 100) || emit(0, 451, 7, 101)) return 1;
    for (mm = 1; mm < 20; ++mm)
        if (s1_make_telemetry(&rejected, 0, mm, 1) != S1_CODEC_INVALID) return 2;
    for (mm = 4001; mm <= 4095; ++mm)
        if (s1_make_telemetry(&rejected, 0, mm, 1) != S1_CODEC_INVALID) return 2;
    for (j = 0; j < 3; ++j) {
        if (s1_pack_heartbeat(counters[j], heartbeat, sizeof(heartbeat)) != 5) return 1;
        printf("{\"kind\":\"heartbeat\",\"counter\":%u,\"hex\":\"", (unsigned)counters[j]);
        for (k = 0; k < sizeof(heartbeat); ++k) printf("%02x", heartbeat[k]);
        puts("\"}");
    }
    for(pitch=-900;pitch<=900;++pitch) {
        if(emit_imu(pitch,12,80,2,0x11,0) || emit_imu(pitch,12,80,2,0x31,0)) return 3;
    }
    for(mm=0;mm<=80;++mm) {
        if(mm>0 && mm<6) continue;
        if(emit_imu(-150,mm,mm,2,0x11,0)) return 3;
    }
    for(j=0;j<=6;++j) for(k=0;k<=0xbfU;++k) {
        if(s1_make_imu(&rejected_imu,150,12,34,j,k,1000,1000)==0 &&
            emit_imu(150,12,34,j,k,0)) return 3;
    }
    if(emit_imu(150,12,34,2,0x11,100) || emit_imu(150,12,34,2,0x11,101) ||
        emit_imu(150,12,34,2,0x11,1000) || emit_imu(150,12,34,2,0x11,1001) ||
        emit_imu(0,0,0,7,0,0)) return 3;
    return emit_status();
}
