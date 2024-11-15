#ifndef CRC_H_
#define CRC_H_

#define CRC5_MASK 0x1F

uint8_t crc5(uint8_t *data, uint8_t len);
uint16_t crc16(const unsigned char *buffer, int len);
uint16_t crc16_false(const unsigned char *buffer, int len);

#endif // PRETTY_H_