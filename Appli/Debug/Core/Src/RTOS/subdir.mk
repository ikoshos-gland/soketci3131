################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/RTOS/acquisition_task.c \
../Core/Src/RTOS/processing_task.c 

OBJS += \
./Core/Src/RTOS/acquisition_task.o \
./Core/Src/RTOS/processing_task.o 

C_DEPS += \
./Core/Src/RTOS/acquisition_task.d \
./Core/Src/RTOS/processing_task.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/RTOS/%.o Core/Src/RTOS/%.su Core/Src/RTOS/%.cyclo: ../Core/Src/RTOS/%.c Core/Src/RTOS/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32H7S3xx -DUSE_NUCLEO_64 -c -I../Core/Inc -I../../Drivers/STM32H7RSxx_HAL_Driver/Inc -I../../Drivers/STM32H7RSxx_HAL_Driver/Inc/Legacy -I../../Drivers/BSP/STM32H7RSxx_Nucleo -I../../Drivers/CMSIS/Device/ST/STM32H7RSxx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-RTOS

clean-Core-2f-Src-2f-RTOS:
	-$(RM) ./Core/Src/RTOS/acquisition_task.cyclo ./Core/Src/RTOS/acquisition_task.d ./Core/Src/RTOS/acquisition_task.o ./Core/Src/RTOS/acquisition_task.su ./Core/Src/RTOS/processing_task.cyclo ./Core/Src/RTOS/processing_task.d ./Core/Src/RTOS/processing_task.o ./Core/Src/RTOS/processing_task.su

.PHONY: clean-Core-2f-Src-2f-RTOS

