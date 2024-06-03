#include <stdio.h>
#include <stdlib.h>
#include <opus/opus.h>
#include <ogg/ogg.h>

#define SAMPLE_RATE 16000
#define CHANNELS 1
#define FRAME_SIZE 960

// 检查错误的宏
#define CHECK_ERR(cond, msg) do { if (cond) { fprintf(stderr, msg "\n"); exit(EXIT_FAILURE); } } while (0)

typedef struct {
    char     capture_pattern[8];   // "OpusHead"
    unsigned char version;         // Version number (0x01 for this version)
    unsigned char channels;        // Number of output channels
    unsigned short pre_skip;       // Number of samples to discard at the beginning
    unsigned int sample_rate;      // Sample rate (Hz)
    unsigned short output_gain;    // Gain to apply to the output (Q8.8 format)
    unsigned char mapping_family;  // Channel Mapping Family
} OpusHeader;

void write_opus_header(FILE *out, ogg_stream_state *os) {
    OpusHeader header = {
        .capture_pattern = "OpusHead",
        .version = 1,
        .channels = CHANNELS,
        .pre_skip = 0,
        .sample_rate = SAMPLE_RATE,
        .output_gain = 0,
        .mapping_family = 0
    };

    unsigned char header_data[19];
    memcpy(header_data, header.capture_pattern, 8);
    header_data[8] = header.version;
    header_data[9] = header.channels;
    header_data[10] = header.pre_skip & 0xFF;
    header_data[11] = (header.pre_skip >> 8) & 0xFF;
    header_data[12] = header.sample_rate & 0xFF;
    header_data[13] = (header.sample_rate >> 8) & 0xFF;
    header_data[14] = (header.sample_rate >> 16) & 0xFF;
    header_data[15] = (header.sample_rate >> 24) & 0xFF;
    header_data[16] = header.output_gain & 0xFF;
    header_data[17] = (header.output_gain >> 8) & 0xFF;
    header_data[18] = header.mapping_family;

    ogg_packet header_packet = {
        .packet = header_data,
        .bytes = sizeof(header_data),
        .b_o_s = 1,
        .e_o_s = 0,
        .granulepos = 0,
        .packetno = 0
    };
    ogg_stream_packetin(os, &header_packet);

    ogg_page og;
    while (ogg_stream_flush(os, &og)) {
        fwrite(og.header, 1, og.header_len, out);
        fwrite(og.body, 1, og.body_len, out);
    }
}

void write_opus_tags(FILE *out, ogg_stream_state *os) {
    const char *vendor_string = "libopus";
    size_t vendor_length = strlen(vendor_string);
    unsigned char tags[16 + vendor_length];
    strcpy((char *)tags, "OpusTags");
    tags[8] = vendor_length & 0xFF;
    tags[9] = (vendor_length >> 8) & 0xFF;
    tags[10] = (vendor_length >> 16) & 0xFF;
    tags[11] = (vendor_length >> 24) & 0xFF;
    memcpy(tags + 12, vendor_string, vendor_length);
    tags[12 + vendor_length] = 0;
    tags[13 + vendor_length] = 0;
    tags[14 + vendor_length] = 0;
    tags[15 + vendor_length] = 0;

    ogg_packet tags_packet = {
        .packet = tags,
        .bytes = sizeof(tags),
        .b_o_s = 0,
        .e_o_s = 0,
        .granulepos = 0,
        .packetno = 1
    };
    ogg_stream_packetin(os, &tags_packet);

    ogg_page og;
    while (ogg_stream_flush(os, &og)) {
        fwrite(og.header, 1, og.header_len, out);
        fwrite(og.body, 1, og.body_len, out);
    }
}

int main() {
    int err;
    //OpusEncoder *encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
    OpusEncoder *encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_AUDIO, &err);
    CHECK_ERR(err != OPUS_OK, "Failed to create Opus encoder");

    // 设置比特率
    err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));
    CHECK_ERR(err != OPUS_OK, "Failed to set Opus encoder bitrate");

    // Ogg stream initialization
    ogg_stream_state os;
    int serial = rand();
    err = ogg_stream_init(&os, serial);
    CHECK_ERR(err != 0, "Failed to initialize Ogg stream");

    FILE *out = fopen("output.ogg", "wb");
    CHECK_ERR(!out, "Failed to open output file");

    // 写 Opus 头信息
    write_opus_header(out, &os);
    write_opus_tags(out, &os);

    // 编码和写入 Opus 数据
    short input[FRAME_SIZE * CHANNELS];
    unsigned char output[4096];
    ogg_page og;
    ogg_packet packet;
    size_t frame_size_in_bytes = FRAME_SIZE * CHANNELS * sizeof(short);

    FILE *  input_file = fopen("input.pcm", "rb");
    if (!input_file) {
        fprintf(stderr, "Failed to open input file\n");
        return 1;
    }

    ogg_int64_t total_samples = 0;
    ogg_int64_t packet_no = 2;
    while (fread(input, 1, frame_size_in_bytes, input_file) == frame_size_in_bytes) {
        int nb_bytes = opus_encode(encoder, input, FRAME_SIZE, output, sizeof(output));
        CHECK_ERR(nb_bytes < 0, "Opus encoding failed");    
        packet.packet = output;
        packet.bytes = nb_bytes;
        packet.b_o_s = 0;
        packet.e_o_s = 0;
        packet.granulepos = total_samples;
        packet.packetno = packet_no++;
        printf("encode %d = %d granulepos %d packetno %d\n", frame_size_in_bytes, nb_bytes, packet.granulepos, packet.packetno);

        ogg_stream_packetin(&os, &packet);

        while (ogg_stream_flush(&os, &og)) {
            fwrite(og.header, 1, og.header_len, out);
            fwrite(og.body, 1, og.body_len, out);
            printf("head %d body %d\r\n", og.header_len, og.body_len);
        }
        total_samples += FRAME_SIZE*3;
    }

    // 处理末尾包
    packet.e_o_s = 1;  // 设置 EOS 标志
    packet.granulepos = total_samples;  // 设置为总样本数
    packet.packetno = packet_no++;
    ogg_stream_packetin(&os, &packet);

    while (ogg_stream_flush_fill(&os, &og, 4096)) {
        fwrite(og.header, 1, og.header_len, out);
        fwrite(og.body, 1, og.body_len, out);
    }

    printf("total_samples %d packet_no %d\n", total_samples, packet_no);

    // 清理
    opus_encoder_destroy(encoder);
    ogg_stream_clear(&os);
    fclose(out);

    printf("compile %s %s\r\n", __DATE__, __TIME__);
    printf("size int %d  long %d\r\n", sizeof(int), sizeof(long));

    return 0;
}
