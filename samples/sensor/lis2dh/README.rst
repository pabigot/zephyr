.. _lis2dh:

LIS2DH: Accelerometer Monitor
#############################

Overview
********

This sample application periodically reads accelerometer data from the
LIS2DH sensor, and displays the sensor data on the console.

Requirements
************

This sample uses the LIS2DH, ST MEMS system-in-package featuring a 3D
digital linear acceleration sensor, controlled using the I2C interface.

References
**********

For more information about the LIS2DH eCompass module, see
http://www.st.com/en/mems-and-sensors/lis2dh.html.

Building and Running
********************

This project outputs sensor data to the console. It requires a LIS2DH
system-in-package, which is present on the stm32f3_disco board.

Building on stm32f3_disco
=========================

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/lis2dh
   :board: stm32f3_disco
   :goals: build
   :compact:

Sample Output
=============

.. code-block:: console

    Polling at 0.5 Hz
    #1 @ 12 ms: x -5.387328 , y 5.578368 , z -5.463744
    #2 @ 2017 ms: x -5.310912 , y 5.654784 , z -5.501952
    #3 @ 4022 ms: x -5.349120 , y 5.692992 , z -5.463744

   <repeats endlessly every 2 seconds>
