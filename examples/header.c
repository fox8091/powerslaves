#include <powerslaves.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h> // getopt

enum behavior {
    DEVICE_NTR = 0,
    DEVICE_TWL = 1,
    DEVICE_CTR = 2,
    DEVICE_AUTO = DEVICE_CTR
};

struct args {
    const char *header_filename;
};

static void parse_args(int argc, char *argv[], struct args *arg) {
    char c;
    while ((c = getopt(argc, argv, "o:")) != -1) {
        switch (c) {
            case '?':
                exit(-1);
            case 'o':
                if (!optarg) exit(EXIT_FAILURE);
                arg->header_filename = optarg;
                break;
        }
    }
}

static const uint8_t CTRmagic[] = {0x71, 0xC9, 0x3F, 0xE9, 0xBB, 0x0A, 0x3B, 0x18};
static const uint8_t dummy_cmd[] = {0x9F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t header_cmd[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t chipid_cmd[] = {0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t switchmode[] = {0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};                 
static const uint8_t header_ctr_cmd[] = {0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; 
//static const uint8_t rekey_ctr_cmd[] = {0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; 
//static const uint8_t id_ctr_cmd[] = {0xA2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const uint8_t nds_walk_with_me_chipid[] = {0x80, 0x3F, 0x01, 0xE0};

#define is_3ds(var) !!((var) & (1<<(28)))
#define combine_array(var) (var[0] | (var[1] << 8) | (var[2] << 16) | (var[3] << 24))

//#define readHeaderNTR(buf, len, offset) uint8_t header_tmp = header_cmd; header_tmp[1] = offset & 0x000000FF; header_tmp[2]powerslaves_sendreceive(NTR, header_cmd, len, buf)
#define readChipID(buf) powerslaves_sendreceive(NTR, chipid_cmd, 4, buf)
#define switchToCTR() powerslaves_send(NTR, switchmode, 4)
#define readHeaderCTR(buf, len) powerslaves_sendreceive(CTR, header_ctr_cmd, len, buf)


void readHeaderNTR(uint8_t* buf, uint32_t len, uint32_t offset){
    uint8_t header_tmp[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    header_tmp[2] = (uint8_t)(offset & 0x000000FF); 
    header_tmp[3] = (uint8_t)((offset & 0x0000FF00) >> 8);
    header_tmp[4] = (uint8_t)((offset & 0x00FF0000) >> 16);
    header_tmp[5] = (uint8_t)((offset & 0xFF000000) >> 24);

    powerslaves_sendreceive(NTR, header_tmp, len, buf);
    
    return;
}

bool is_dsi(uint32_t var){
    if(var == (uint32_t)combine_array(nds_walk_with_me_chipid)){
        return false;
    } else if(!!((var) & (1<<(30)))){
        return true;
    } else {
        return false;
    }
}

bool arrayCompare(uint8_t* array, uint8_t size){
    bool equal = true;
    for(int i = 1; i < size; i++){
        if(array[0] != array[i]){
            equal = false;
            break;
        }
    }
    return equal;
}

int main(int argc, char *argv[]) {
    if (powerslaves_select(NULL)) {
        puts("Failed to initialize powerslaves!");
        exit(EXIT_FAILURE);
    }

    struct args arg = {
        .header_filename = NULL,
    };
    parse_args(argc, argv, &arg);
    uint32_t header_len;
    char header_filename[13];

    uint8_t *garbage = (uint8_t*)malloc(0x2000);
    if (!garbage) {
        puts("Memory allocation failure!");
        exit(EXIT_FAILURE);
    }
    
    powerslaves_mode(ROM_MODE);
    powerslaves_sendreceive(NTR, dummy_cmd, 0x2000, garbage);
    free(garbage);

    uint8_t chipid[0x4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t chipid2[0x4] = {0x00, 0x00, 0x00, 0x00};
    bool inconsistentChipid = false;

    powerslaves_send(NTR, CTRmagic, 0);
    readChipID(chipid);
    if(!arg.header_filename){
        snprintf(header_filename, 13, "%02x%02x%02x%02x.bin", chipid[0], chipid[1], chipid[2], chipid[3]);
    }
    if(arrayCompare(chipid, sizeof(chipid)) && !chipid[0]){
        printf("Cartridge slot is empty!\n");
        exit(EXIT_FAILURE);
    }
    readChipID(chipid2);
    if(!!(uint8_t)memcmp(chipid, chipid2, sizeof(chipid))){
        inconsistentChipid = true;
        printf("This is a flashcart!\n");
    }
    bool is3DS = is_3ds(combine_array(chipid));
    bool isDSi = is_dsi(combine_array(chipid));
    if(arrayCompare(chipid, sizeof(chipid))){
        printf("Something is wrong, forcing DS mode...\n");
        is3DS = false;
    }

    header_len = is3DS ? 0x200 + 0x40 : (isDSi ? 0x1000 : 0x200);
    uint8_t *header = (uint8_t*)malloc(isDSi ? header_len + 1 : header_len);
    if (!header) {
        puts("Memory allocation failure!");
        exit(EXIT_FAILURE);
    }
    printf("Reading 0x%x byte header to file '%s'.\n", is3DS ? header_len - 0x40 : header_len, !!arg.header_filename ? arg.header_filename : header_filename);

    if(is3DS){
        switchToCTR();
        readHeaderCTR(header, header_len);
    } else if(isDSi) {
        for(uint32_t i = 0; i < header_len; i += 0x200){
            readHeaderNTR(header +  i, 0x201, i);
            for(uint32_t j = 1; j < 0x201; j++){
                header[i+j-1] = header[i+j];
            }
        }
    } else {
        readHeaderNTR(header, header_len, 0);
    }
    printf("ChipID: %02x%02x%02x%02x\n",
        chipid[0], chipid[1], chipid[2], chipid[3]);
    if(inconsistentChipid){
        printf("ChipID (read 2): %02x%02x%02x%02x\n",
            chipid2[0], chipid2[1], chipid2[2], chipid2[3]);
    }

    FILE *headerfile = fopen(!!arg.header_filename ? arg.header_filename : header_filename, "wb");
    fwrite(is3DS ? header + 0x40 : header, is3DS ? header_len - 0x40 : header_len, 1, headerfile);

    free(header);
    powerslaves_exit();

    return 0;
}
