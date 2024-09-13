#ifndef MAX6689_H_
#define MAX6689_H_

#define MAX6689_I2CADDR 0x4D
#define MAX6689_ERR_OPEN 0xFF
#define MAX6689_ERR_SHORT 0xEE

int max6689_init();
float max6689_read_temp(uint8_t);

#endif /* MAX6689_H_ */