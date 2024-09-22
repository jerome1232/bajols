/*
 * The TelemetryStreamTest application.
 *
 * Copyright (C) 2024 Jeremy D. Jones <j.jones1232@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Download IBusBM library here
 * https://github.com/bmellink/IBusBM
 *
 * Click "<> Code" -> Download zip
 * Add to sketch in Arduino ide
 * Sketch -> Include Library -> Add .zip library
 */
#include <IBusBM.h>
#include <math.h>
#include "motor.h"

/*
 * Version number
 */
const String VERSION = "0.0.3";

/* 
 * Constant used to convert
 * signed 32 bit GPS integers to a 
 * decimial notation.
 */
const double SCALE_INT_GPS_TO_DEC = 1E-7;

/* 
 * Constant used to convert
 * decimal GPS data to an
 * integer notation.
 */
const uint32_t SCALE_DEC_GPS_TO_INT = 1E7;

/*
 * Constant used to scale meters to centimeters
 */
const uint8_t SCALE_METERS_TO_CENTIMETERS = 100;

/* 
 * Extended GPS Sensor addresses 
 * I have no idea if these ones will work
 * Data type noted inline is the data type of the
 * data sent to sensor
 */

/* Latitude telmetry address Data is type int32_t scaled by 1E-7*/
const uint8_t IBUS_SENSOR_TYPE_GPS_LAT = 0x80;
const uint8_t IBUS_SENSOR_TYPE_GPS_LAT_SIZE = 4; // This sensor uses 4 bytes

/* Longitude telmetry address Data is type int32_t scaled by 1E-7*/
const uint8_t IBUS_SENSOR_TYPE_GPS_LON = 0x81;
const uint8_t IBUS_SENSOR_TYPE_GPS_LON_SIZE = 4; // This sensor uses 4 bytes

/* Distance from origin telmetry address Data is type uint16_t Unit is centimeters*/
const uint8_t IBUS_SENSOR_TYPE_GPS_DIST = 0x14;

/* Altitude telemetry address Data is type int32_t Unit is meters*/
const uint8_t IBUS_SENSOR_TYPE_GPS_ALT = 0x82;
const uint8_t IBUS_SENSOR_TYPE_GPS_ALT_SIZE = 4; // This sensor uses 4 bytes

/*
 * Inteligent Bus used to communicate with a
 * FlySky reciever
 */
IBusBM IBus;

/* Amount of time to delay between sensor updates */
const uint16_t WAIT_TIME = 5000;

/* Serial line baudrate */
const uint32_t BAUDRATE = 115200;

/*
 * This is a "fake" gps location, I actually just grabbed it off
 * of google maps, it's somewhere in smithfield
 */

/* Home latitude */
const int32_t ORIGIN_LAT = 418437575;

/* Home longitude */
const int32_t ORIGIN_LON = -1118449990;

/* Fake latitude gps data */
int32_t lat = ORIGIN_LAT;

/* Fake longitude gps data */
int32_t lon = ORIGIN_LON;

/* Fake alitiude gps data in meters */
int32_t alt = 440;

/* Fake rpm sensor data */
uint16_t rpm = 0;

/* Size of array holding channel data */
const int8_t CHANNEL_DATA_SIZE = 6;

/* Holds the lowest values seen for each channel */
int16_t minChannelData[CHANNEL_DATA_SIZE] = {0, 0, 0, 0, 0, 0};

/* Holds the highest values seen for each channel */
int16_t maxChannelData[CHANNEL_DATA_SIZE] = {0, 0, 0, 0, 0, 0};

/* The main screw */
Motor::HBridgePWM engine;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(BAUDRATE);

  // I believe on a Mega this will need to be Serial1 UPDATE_ME_IF_COMPILE_FAILS
  IBus.begin(Serial1);

  Serial.println("~");
  Serial.print("Initializing TelemetryStreamTest v");
  Serial.println(VERSION);
  Serial.println("~");
  Serial.println();

  Serial.println("Adding base sensors");
  IBus.addSensor(IBUSS_RPM);
  Serial.println("Base sensors added");

  Serial.println("Adding extended gps sensors");
  IBus.addSensor(IBUS_SENSOR_TYPE_GPS_LAT, IBUS_SENSOR_TYPE_GPS_LAT_SIZE);
  IBus.addSensor(IBUS_SENSOR_TYPE_GPS_LON, IBUS_SENSOR_TYPE_GPS_LON_SIZE);
  IBus.addSensor(IBUS_SENSOR_TYPE_GPS_ALT, IBUS_SENSOR_TYPE_GPS_ALT_SIZE);
  IBus.addSensor(IBUS_SENSOR_TYPE_GPS_DIST);
  Serial.println("Extended gps sensors added");

  Serial.println("Sensors initialized");
  Serial.print("Waiting ");
  Serial.print(WAIT_TIME * 2);
  Serial.println(" milliseconds");
  Serial.println("I think you should be able to find and add sensors to screen on controller now?");
  delay(WAIT_TIME * 2);

  Serial.print("Streaming telemetry and reading remote every");
  Serial.print(WAIT_TIME);
  Serial.println(" milliseconds");
}

void loop() {
  Serial.println("Computing GPS distance from Origin");
  Serial.println("##################################");

  // I chose to scale this to centimeters for a little more precision
  const int16_t distance = computeAproxDistanceCentimeters(ORIGIN_LAT, ORIGIN_LON, lat, lon);

  printSensorData(distance);

  Serial.println("Setting RPM base sensor");
  setBaseSensors();
  Serial.println("RPM base sensor set");

  /* 
   * I'm worried these extended sensors won't work
   * with flysky remote
   */
  Serial.println("Setting GPS extended sensors");
  setGpsSensors(distance);
  Serial.println("GPS extended sensors set");

  incrementBaseSensors();
  incrementGpsSensors();

  Serial.print("Wait ");
  Serial.print(WAIT_TIME);
  Serial.println(" ms");
  delay(WAIT_TIME);

  channelReader();

  Serial.print("Wait ");
  Serial.print(WAIT_TIME);
  Serial.println(" ms");
  delay(WAIT_TIME);
  
  engine.set(Motor::FORWARD, Motor::MAX_PWM_VALUE);
}

/*
 * Read data from iBus channels, record highest and lowest values
 */
void channelReader() {
  Serial.println("Reading data from all 6 channels");
  int16_t channelData[CHANNEL_DATA_SIZE] = {0, 0, 0, 0, 0, 0};
  for (int8_t i = 0; i < CHANNEL_DATA_SIZE; i++) {
    channelData[i] = IBus.readChannel(i);

    if (channelData[i] < minChannelData[i])
    {
      minChannelData[i] = channelData[i];
    }

    if (channelData[i] > maxChannelData[i])
    {
      maxChannelData[i] = maxChannelData[i];
    }

    Serial.print("Channel ");
    Serial.print(i);
    Serial.print(" = ");
    Serial.print(channelData[i]);
    Serial.print(" MAX: ");
    Serial.print(maxChannelData[i]);
    Serial.print(" MIN: ");
    Serial.println(minChannelData[i]);
  }
}

/*
 * Send data to reciver for RPMs
 */
void setBaseSensors() {
  IBus.setSensorMeasurement(IBUSS_RPM, rpm);
}

/*
 * Increment fake rpm sensor data
 */
void incrementBaseSensors() {
  rpm++;
}

/*
 * Send data to reciever for GPS
 */
void setGpsSensors(const int16_t dist) {
  IBus.setSensorMeasurement(IBUS_SENSOR_TYPE_GPS_LAT, lat);
  IBus.setSensorMeasurement(IBUS_SENSOR_TYPE_GPS_LON, lon);
  IBus.setSensorMeasurement(IBUS_SENSOR_TYPE_GPS_ALT, alt);
  IBus.setSensorMeasurement(IBUS_SENSOR_TYPE_GPS_DIST, dist);
}

/*
 * Change GPS data
 */
void incrementGpsSensors() {
  lat += 11;
  lon += 5;
  alt += 52;
}

/*
 * Compute aprox distance from origin
 *
 * Note:
 * I should figure out a way to do all of this math with ints but it seems fast enough for now
 * This might be functionality done automatically by  a GPS module anyways.
 */
int16_t computeAproxDistanceCentimeters(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  // Adjust 32 bit integers to a decimal representation
  const double fixedLat1 = double(lat1) * SCALE_INT_GPS_TO_DEC;
  const double fixedLon1 = double(lon1) * SCALE_INT_GPS_TO_DEC;
  const double fixedLat2 = double(lat2) * SCALE_INT_GPS_TO_DEC;
  const double fixedLon2 = double(lon2) * SCALE_INT_GPS_TO_DEC;

  Serial.println("ORIGIN");
  Serial.print("Lat:Long=");
  Serial.print(fixedLat1, 7);
  Serial.print(",");
  Serial.println(fixedLon1, 7);

  Serial.println("NEW");
  Serial.print("Lat:Long=");
  Serial.print(fixedLat2, 7);
  Serial.print(",");
  Serial.println(fixedLon2, 7);

  // Formula for aprox distance: https://en.wikipedia.org/wiki/Geographic_coordinate_system#Latitude_and_longitude
  const double avgLat = (fixedLat1 + fixedLat2) / 2.0;
  const double mPerDegreeLat = abs(111132.92 - 559.82 * cos(2.0 * avgLat) + 1.175 * cos(4.0 * avgLat) - 0.0023 * cos(6.0 * avgLat));
  const double mPerDegreeLon = abs(111412.84 * cos(avgLat) - 93.5 * cos(3.0 * avgLat) + 0.118 * cos(5.0 * avgLat));

  // Compute change in Lat/Lon
  double deltaLat = abs(fixedLat1 - fixedLat2);
  double deltaLon = abs(fixedLon1 - fixedLon2);

  Serial.print("Change in Lat:");
  Serial.println(deltaLat, 7);
  Serial.print("Change in Lon:");
  Serial.println(deltaLon, 7);

  Serial.print("Meters per Degree Lat,Lon:");
  Serial.print(mPerDegreeLat, 7);
  Serial.print(",");
  Serial.println(mPerDegreeLon, 7);

  // Compute distance between points
  return int16_t(sqrt(pow(deltaLat * mPerDegreeLat, 2) + pow(deltaLon * mPerDegreeLon, 2)) * SCALE_METERS_TO_CENTIMETERS);
}

/*
 * Print all sensor data to serial monitor
 */
void printSensorData(const uint16_t dist) {
  Serial.print("Rpm=");
  Serial.println(rpm);

  Serial.print("lat=");
  Serial.println(double(lat) * SCALE_INT_GPS_TO_DEC, 7);

  Serial.print("lon=");
  Serial.println(double(lon) * SCALE_INT_GPS_TO_DEC, 7);

  Serial.print("alt=");
  Serial.print(double(alt) / 100.0);
  Serial.println(" m");

  Serial.print("Distance=");
  Serial.print(dist);
  Serial.println(" cm");
}