
#include "esp_log.h"
#include "i2c_master.h"
#include "max6689.h"

static const char * TAG = "MAX6689";

int max6689_init(){
    uint8_t mid;
    ESP_ERROR_CHECK(i2c_master_register_read(MAX6689_I2CADDR, MAX6689_MANUFACTURER_ADDR,&mid,1));
    if(mid==MAX6689_MANUFACTURER_ID){
        ESP_LOGI(TAG, "MAX6689UP9A+ founed");
    }

    ESP_LOGI(TAG, "Masking ALERT and OVERT and using program monitor.");
    ESP_ERROR_CHECK(i2c_master_register_write_byte(MAX6689_I2CADDR,MAX6689_CONFIG2_ADDR,MAX6689_ALERT_MASK));
    ESP_ERROR_CHECK(i2c_master_register_write_byte(MAX6689_I2CADDR,MAX6689_CONFIG3_ADDR,MAX6689_OVERT_MASK));

    // ESP_LOGI(TAG, "Checking Temp Diodes status:");
    // uint8_t temp[6]={0,0,0,0,0,0};
    // max6689_read_temp(temp);
    // bool hasFault=false;
    // for(int a=0;a<6;a++){
    //     if(temp[a]==MAX6689_ERR_OPEN){
    //         ESP_LOGE(TAG, "Chip: %d diode open circuit fault", a);
    //         hasFault=true;
    //     }
            
    //     else if(temp[a]==MAX6689_ERR_SHORT){
    //         ESP_LOGE(TAG, "Chip: %d diode short circuit fault", a);
    //         hasFault=true;
    //     }
    //     else
    //         ESP_LOGI(TAG, "Chip: %d diode looks good!",a);
    // }

    // if(hasFault){
    //     exit(EXIT_FAILURE);
    // }
    return 0;
}

int max6689_read_temp(uint8_t* outData){
    for(uint8_t a=1; a<7;a++){
        uint8_t temp=0;
        ESP_ERROR_CHECK(i2c_master_register_read(MAX6689_I2CADDR, a ,&temp,1));
        outData[a-1]=temp;
    }
    return 0;
}

    