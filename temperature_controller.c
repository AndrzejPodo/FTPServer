#include <stdio.h>
#include <wiringPiI2C.h>
#include "temperature_controller.h"
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

void* temperatureControllerThread(void *vargp)
{
    int fd = wiringPiI2CSetup(DEVICE_ID);

    if (fd == -1)
    {
        printf("Failed to init I2C communication.\n");
        return -1;
    }

    printf("Successfully initialized I2C communication.\n");

    int* exit_code = (int* ) malloc(1);
    int received_data;
    while (1) {
        received_data = wiringPiI2CReadReg16(fd, 0);

        if (received_data > 3000) {
            *exit_code = received_data;
            pthread_exit(exit_code);
        } 
        sleep(1);
    }

}