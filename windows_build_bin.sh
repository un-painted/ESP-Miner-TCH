E:\esp-idf\components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py generate .\config204-empty.csv .\build\config.bin 0x6000

python.exe D:\espressif5.1.1\v5.1.1\esp-idf\components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py generate .\config204-empty.csv .\build\config.bin 0x6000

esptool.exe --chip esp32s3 merge_bin --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x9000 build/config303.bin 0x10000 build/esp-miner.bin 0x410000 build/www.bin 0xf10000 build/ota_data_initial.bin -o "esp-miner-factory-v2.1.10-TCH-All-in-one.bin"