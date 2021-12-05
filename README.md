# esp32-assembler

This is the right place if you want to try out assembler programming.

The [ESP32](https://www.espressif.com/en/products/socs/esp32) contains an ultra low power (ULP) coprocessor that needs to be programmed in assembler. This project transforms the ESP32 into a compiler that allows you to enter your instructions in a terminal. You can execute your code on the ULP and inspect the memory (e.g. to check shared variables). All you need is an ESP32 (e.g. [NodeMCU ESP32](https://joy-it.net/de/products/SBC-NodeMCU-ESP32), a computer with an USB interface and a terminal program (e.g. [putty](https://www.putty.org/)).

A typical workflow is ...

1. write your code in a text editor
2. copy the code to the clipboard
3. connect the ESP32 to your computer and start a terminal program
4. paste the clipboard content to the terminal program
5. run the program 
6. inspect the memory

An example:

![terminal demo](images/terminal_demo.jpg)

## Transferring the code on your ESP32 without building it

You do not have to build this project before using it! 

All you need are ...

* the binary files (available in the "binaries" folder)
* [Espressif Flash Download Tools](https://www.espressif.com/en/support/download/other-tools)

Download the artifacts mentioned above and start the Flash Download Tools. You'll have to provide the paths and target addresses of the binary files.

| binary             | address       |
| -------------------|:-------------:|
| bootloader.bin     | 0x1000        |
| partition-table.bin| 0x8000        |
| esp32-assembler.bin| 0x10000       |

The PDF (shipped together with the tools) of the Flash Download Tools provides more detailed descriptions how to use it.

## Supported commands

In addition to the [ESP32 ULP coprocessor instruction set](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ulp_instruction_set.html), the following commands are available.

| command                     | description                                                             |
|-----------------------------|-------------------------------------------------------------------------|
| var(\<value\>)              | Stores "value" (which is an integer in the range 0 - 65535) at the current command index. |  
| run \<index\>               | Executes your program and displays the memory used by it. The argument "index" defines the index (starts counting at 0) of the first command to execute. |  
| list                        | Displays the memory used by your program .                              |   
| reset                       | Removes all already entered commands (the same as restarting the ESP32).|  

## What's happening behind the scene

The ESP32 consists of 2 CPUs and the ULP coprocessor. Both CPUs support a rich set of commands and can get programmed by using different languages (e.g. C). In contrast to that, the ULP coprocessor has a very limited set of supported instructions to reduce its power consumption to a minimum. This enables us to put the 2 powerful CPUs into deep sleep while the ULP coprocessor is performing some easy task (e.g. read values from a sensor and store them). As soon as the ULP corpcessor finishes its task, it wakes up the 2 CPUs and they continue data processing. The usage of the ULP coprocessor reduces the power consumption a lot and makes it easier to run the ESP32 from a battery.

As an example you can have a look at the [Windsensor project](https://github.com/tederer/windsensor). In this project the 2 CPUs of the ESP32 periodically go to deep sleep. While the CPUs are sleeping the ULP coprocessor collects data from a windsensor and stores the data in the memory. After a minute, the ULP coprocessor wakes up the CPUs and they format and deliver the sensor data via HTTP to a server.

The ULP coprocessor consists of 4 general purpose 16 bit registers and one 8 bit counter register. Program code and data get stored together in memory that is accessible by the ULP coprocessor and the 2 CPUs. 

When you write your ULP program, first you should think about what data you need to pass from the CPUs to the ULP coprocessor and vice versa. Independed from the direction of data passing, you'll need to use some memory to store the data. For this purpose the `var(<value>)` command was created. Adding it to your ULP coprocessor code writes the 16 bit value to the current position in you program. Don't be surprised that the 16 bit values takes 4 bytes in memory. That's ok, because the ULP coprocessor uses a memory alignment of 4 bytes. You can ignore the upper two bytes ... they "only" contain some meta information in case the value was stored by the `st` command.

After adding the variable, it's time to add your instructions to your program. The ESP32 reads your input line by line and adds each valid command to a memory of the CPUs (not available for the ULP coprocessor!). For each entered command you'll get a message in the terminal telling you the index of the command. You'll need that index for example in jump commands.

When you added your last command, then it's time to bring the code and the data into the memory that is accessible by the ULP coprocessor and the CPUs and run it. This can be done by calling the `run <index>` command. All you need to supply it the index of the first command in your program. It will be 0 if you have no variables infront of the code. If your program starts with variables, then you have to use the index you got for the first instruction you entered.

Attention: Some instructions (e.g. JUMPR) use distances in bytes instead of commands. In such a case you'll have to multiply the distance (in commands) by 4 (because each command consists of 4 bytes).

The run command copies your program to the memoray accessible by the ULP coprocessor and the CPUs and starts the ULP coprocessor. After 500ms the memory, used by your program, gets dumped to the terminal.

Note: If your ULP coprocessor code runs longer than 500ms, then the memory dump will not reflect the state at the end of the program because it still gets executed. In such a case, wait till execution finished and use the `list` command to get the memory dump.

For more details please have a look at the chapter "ULP Coprocessor (ULP)" in the  [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf).


## Building the code in the docker image

To build this project you need [docker](https://www.docker.com). Before you start the development container, please check the "projectRootInHost" path in `startDevEnvInDocker.sh` and correct it if necessary. This path should point to the directory containing this project.

Connect your ESP32 via USB to your computer and check if "/dev/ttyUSB0" exists.

When you execute `startDevEnvInDocker.sh`, a docker container containing the development dependencies (for details have a look at the [container project](https://github.com/tederer/esp32dev)) will start and the project automtically gets mounted into the container using /root/esp/esp32-assembler/.

Call `idf.py flash` to build and transfer the software to your ESP32.

## References

[ESP32 ULP coprocessor instruction set](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ulp_instruction_set.html)

[ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)