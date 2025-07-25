/* packet-bluetooth.c
 * Routines for the Bluetooth
 *
 * Copyright 2014, Michal Labedzki for Tieto Corporation
 *
 * Dissector for Bluetooth High Speed over wireless
 * Copyright 2012 intel Corp.
 * Written by Andrei Emeltchenko at intel dot com
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <string.h>
#include <epan/packet.h>
#include <epan/prefs.h>
#include <epan/uat.h>
#include <epan/to_str.h>
#include <epan/conversation_table.h>
#include <epan/decode_as.h>
#include <epan/proto_data.h>
#include <epan/unit_strings.h>
#include <wiretap/wtap.h>
#include "packet-llc.h"
#include <epan/oui.h>

#include <wsutil/str_util.h>

#include "packet-bluetooth.h"

static dissector_handle_t bluetooth_handle;
static dissector_handle_t bluetooth_bthci_handle;
static dissector_handle_t bluetooth_btmon_handle;
static dissector_handle_t bluetooth_usb_handle;

int proto_bluetooth;

static int hf_bluetooth_src;
static int hf_bluetooth_dst;
static int hf_bluetooth_addr;
static int hf_bluetooth_src_str;
static int hf_bluetooth_dst_str;
static int hf_bluetooth_addr_str;

static int hf_llc_bluetooth_pid;

static int ett_bluetooth;

static dissector_handle_t btle_handle;
static dissector_handle_t hci_usb_handle;

static dissector_table_t bluetooth_table;
static dissector_table_t hci_vendor_table;
dissector_table_t        bluetooth_uuid_table;

static wmem_tree_t *chandle_sessions;
static wmem_tree_t *chandle_to_bdaddr;
static wmem_tree_t *chandle_to_mode;
static wmem_tree_t *shandle_to_chandle;
static wmem_tree_t *bdaddr_to_name;
static wmem_tree_t *bdaddr_to_role;
static wmem_tree_t *localhost_name;
static wmem_tree_t *localhost_bdaddr;
static wmem_tree_t *hci_vendors;
static wmem_tree_t *cs_configurations;

wmem_tree_t *bluetooth_uuids;

static int bluetooth_tap;
int bluetooth_device_tap;
int bluetooth_hci_summary_tap;

// UAT structure
typedef struct _bt_uuid_t {
    char *uuid;
    char *label;
    bool long_attr;
} bt_uuid_t;
static bt_uuid_t *bt_uuids;
static unsigned num_bt_uuids;

static bluetooth_uuid_t get_bluetooth_uuid_from_str(const char *str);

// Registery updated to published status of 17 July 2024

const value_string bluetooth_uuid_vals[] = {
    /* Protocol Identifiers - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/protocol_identifiers.yaml */
    { 0x0001,   "SDP" },
    { 0x0002,   "UDP" },
    { 0x0003,   "RFCOMM" },
    { 0x0004,   "TCP" },
    { 0x0005,   "TCS-BIN" },
    { 0x0006,   "TCS-AT" },
    { 0x0007,   "ATT" },
    { 0x0008,   "OBEX" },
    { 0x0009,   "IP" },
    { 0x000A,   "FTP" },
    { 0x000C,   "HTTP" },
    { 0x000E,   "WSP" },
    { 0x000F,   "BNEP" },
    { 0x0010,   "UPNP" },
    { 0x0011,   "HID Protocol" },
    { 0x0012,   "Hardcopy Control Channel" },
    { 0x0014,   "Hardcopy Data Channel" },
    { 0x0016,   "Hardcopy Notification Channel" },
    { 0x0017,   "AVCTP" },
    { 0x0019,   "AVDTP" },
    { 0x001B,   "CMTP" },
    { 0x001D,   "UDI C-Plane" },
    { 0x001E,   "MCAP Control Channel" },
    { 0x001F,   "MCAP Data Channel" },
    { 0x0100,   "L2CAP" },
    /* Service Class - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/service_class.yaml */
    { 0x1000,   "Service Discovery Server Service Class ID" },
    { 0x1001,   "Browse Group Descriptor Service Class ID" },
    { 0x1002,   "Public Browse Group" },
    { 0x1101,   "Serial Port" },
    { 0x1102,   "LAN Access Using PPP" },
    { 0x1103,   "Dial-Up Networking" },
    { 0x1104,   "IrMC Sync" },
    { 0x1105,   "OBEX Object Push" },
    { 0x1106,   "OBEX File Transfer" },
    { 0x1107,   "IrMC Sync Command" },
    { 0x1108,   "Headset" },
    { 0x1109,   "Cordless Telephony" },
    { 0x110A,   "Audio Source" },
    { 0x110B,   "Audio Sink" },
    { 0x110C,   "A/V Remote Control Target" },
    { 0x110D,   "Advanced Audio Distribution" },
    { 0x110E,   "A/V Remote Control" },
    { 0x110F,   "A/V Remote Control Controller" },
    { 0x1110,   "Intercom" },
    { 0x1111,   "Fax" },
    { 0x1112,   "Headset Audio Gateway" },
    { 0x1113,   "WAP" },
    { 0x1114,   "WAP CLIENT" },
    { 0x1115,   "PANU" },
    { 0x1116,   "NAP" },
    { 0x1117,   "GN" },
    { 0x1118,   "Direct Printing" },
    { 0x1119,   "Reference Printing" },
    { 0x111A,   "Imaging" },
    { 0x111B,   "Imaging Responder" },
    { 0x111C,   "Imaging Automatic Archive" },
    { 0x111D,   "Imaging Referenced Objects" },
    { 0x111E,   "Hands-Free" },
    { 0x111F,   "AG Hands-Free" },
    { 0x1120,   "Direct Printing Referenced Objects Service" },
    { 0x1121,   "Reflected UI" },
    { 0x1122,   "Basic Printing" },
    { 0x1123,   "Printing Status" },
    { 0x1124,   "HID" },
    { 0x1125,   "Hardcopy Cable Replacement" },
    { 0x1126,   "HCR Print" },
    { 0x1127,   "HCR Scan" },
    { 0x1128,   "Common ISDN Access" },
    { 0x1129,   "Video Conferencing GW" },
    { 0x112A,   "UDI MT" },
    { 0x112B,   "UDI TA" },
    { 0x112C,   "Audio/Video" },
    { 0x112D,   "SIM Access" },
    { 0x112E,   "Phonebook Access Client" },
    { 0x112F,   "Phonebook Access Server" },
    { 0x1130,   "Phonebook Access Profile" },
    { 0x1131,   "Headset - HS" },
    { 0x1132,   "Message Access Server" },
    { 0x1133,   "Message Notification Server" },
    { 0x1134,   "Message Access Profile" },
    { 0x1135,   "GNSS" },
    { 0x1136,   "GNSS Server" },
    { 0x1137,   "3D Display" },
    { 0x1138,   "3D Glasses" },
    { 0x1139,   "3D Synch Profile" },
    { 0x113A,   "Multi Profile Specification" },
    { 0x113B,   "MPS" },
    { 0x113C,   "CTN Access Service" },
    { 0x113D,   "CTN Notification Service" },
    { 0x113E,   "Calendar Tasks and Notes Profile" },
    { 0x1200,   "PnP Information" },
    { 0x1201,   "Generic Networking" },
    { 0x1202,   "Generic File Transfer" },
    { 0x1203,   "Generic Audio" },
    { 0x1204,   "Generic Telephony" },
    { 0x1205,   "UPNP Service" },
    { 0x1206,   "UPNP IP Service" },
    { 0x1300,   "ESDP UPNP IP PAN" },
    { 0x1301,   "ESDP UPNP IP LAP" },
    { 0x1302,   "ESDP UPNP L2CAP" },
    { 0x1303,   "Video Source" },
    { 0x1304,   "Video Sink" },
    { 0x1305,   "Video Distribution" },
    { 0x1400,   "HDP" },
    { 0x1401,   "HDP Source" },
    { 0x1402,   "HDP Sink" },
    /* Mesh Profile - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/mesh_profile_uuids.yaml */
    { 0x1600,   "Ambient Light Sensor NLC Profile 1.0" },
    { 0x1601,   "Basic Lightness Controller NLC Profile 1.0" },
    { 0x1602,   "Basic Scene Selector NLC Profile 1.0" },
    { 0x1603,   "Dimming Control NLC Profile 1.0" },
    { 0x1604,   "Energy Monitor NLC Profile 1.0" },
    { 0x1605,   "Occupancy Sensor NLC Profile 1.0" },
    /* Service - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/service_uuids.yaml */
    { 0x1800,   "GAP" },
    { 0x1801,   "GATT" },
    { 0x1802,   "Immediate Alert" },
    { 0x1803,   "Link Loss" },
    { 0x1804,   "Tx Power" },
    { 0x1805,   "Current Time" },
    { 0x1806,   "Reference Time Update" },
    { 0x1807,   "Next DST Change" },
    { 0x1808,   "Glucose" },
    { 0x1809,   "Health Thermometer" },
    { 0x180A,   "Device Information" },
    { 0x180D,   "Heart Rate" },
    { 0x180E,   "Phone Alert Status" },
    { 0x180F,   "Battery" },
    { 0x1810,   "Blood Pressure" },
    { 0x1811,   "Alert Notification" },
    { 0x1812,   "Human Interface Device" },
    { 0x1813,   "Scan Parameters" },
    { 0x1814,   "Running Speed and Cadence" },
    { 0x1815,   "Automation IO" },
    { 0x1816,   "Cycling Speed and Cadence" },
    { 0x1818,   "Cycling Power" },
    { 0x1819,   "Location and Navigation" },
    { 0x181A,   "Environmental Sensing" },
    { 0x181B,   "Body Composition" },
    { 0x181C,   "User Data" },
    { 0x181D,   "Weight Scale" },
    { 0x181E,   "Bond Management" },
    { 0x181F,   "Continuous Glucose Monitoring" },
    { 0x1820,   "Internet Protocol Support" },
    { 0x1821,   "Indoor Positioning" },
    { 0x1822,   "Pulse Oximeter" },
    { 0x1823,   "HTTP Proxy" },
    { 0x1824,   "Transport Discovery" },
    { 0x1825,   "Object Transfer" },
    { 0x1826,   "Fitness Machine" },
    { 0x1827,   "Mesh Provisioning" },
    { 0x1828,   "Mesh Proxy" },
    { 0x1829,   "Reconnection Configuration" },
    { 0x183A,   "Insulin Delivery" },
    { 0x183B,   "Binary Sensor" },
    { 0x183C,   "Emergency Configuration" },
    { 0x183D,   "Authorization Control" },
    { 0x183E,   "Physical Activity Monitor" },
    { 0x183F,   "Elapsed Time" },
    { 0x1840,   "Generic Health Sensor" },
    { 0x1843,   "Audio Input Control" },
    { 0x1844,   "Volume Control" },
    { 0x1845,   "Volume Offset Control" },
    { 0x1846,   "Coordinated Set Identification" },
    { 0x1847,   "Device Time" },
    { 0x1848,   "Media Control" },
    { 0x1849,   "Generic Media Control" },
    { 0x184A,   "Constant Tone Extension" },
    { 0x184B,   "Telephone Bearer" },
    { 0x184C,   "Generic Telephone Bearer" },
    { 0x184D,   "Microphone Control" },
    { 0x184E,   "Audio Stream Control" },
    { 0x184F,   "Broadcast Audio Scan" },
    { 0x1850,   "Published Audio Capabilities" },
    { 0x1851,   "Basic Audio Announcement" },
    { 0x1852,   "Broadcast Audio Announcement" },
    { 0x1853,   "Common Audio" },
    { 0x1854,   "Hearing Access" },
    { 0x1855,   "Telephony and Media Audio" },
    { 0x1856,   "Public Broadcast Announcement" },
    { 0x1857,   "Electronic Shelf Label" },
    { 0x1858,   "Gaming Audio" },
    { 0x1859,   "Mesh Proxy Solicitation" },
    /* Units - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/units.yaml */
    { 0x2700,   "unitless" },
    { 0x2701,   "length (metre)" },
    { 0x2702,   "mass (kilogram)" },
    { 0x2703,   "time (second)" },
    { 0x2704,   "electric current (ampere)" },
    { 0x2705,   "thermodynamic temperature (kelvin)" },
    { 0x2706,   "amount of substance (mole)" },
    { 0x2707,   "luminous intensity (candela)" },
    { 0x2710,   "area (square metres)" },
    { 0x2711,   "volume (cubic metres)" },
    { 0x2712,   "velocity (metres per second)" },
    { 0x2713,   "acceleration (metres per second squared)" },
    { 0x2714,   "wavenumber (reciprocal metre)" },
    { 0x2715,   "density (kilogram per cubic metre)" },
    { 0x2716,   "surface density (kilogram per square metre)" },
    { 0x2717,   "specific volume (cubic metre per kilogram)" },
    { 0x2718,   "current density (ampere per square metre)" },
    { 0x2719,   "magnetic field strength (ampere per metre)" },
    { 0x271A,   "amount concentration (mole per cubic metre)" },
    { 0x271B,   "mass concentration (kilogram per cubic metre)" },
    { 0x271C,   "luminance (candela per square metre)" },
    { 0x271D,   "refractive index" },
    { 0x271E,   "relative permeability" },
    { 0x2720,   "plane angle (radian)" },
    { 0x2721,   "solid angle (steradian)" },
    { 0x2722,   "frequency (hertz)" },
    { 0x2723,   "force (newton)" },
    { 0x2724,   "pressure (pascal)" },
    { 0x2725,   "energy (joule)" },
    { 0x2726,   "power (watt)" },
    { 0x2727,   "electric charge (coulomb)" },
    { 0x2728,   "electric potential difference (volt)" },
    { 0x2729,   "capacitance (farad)" },
    { 0x272A,   "electric resistance (ohm)" },
    { 0x272B,   "electric conductance (siemens)" },
    { 0x272C,   "magnetic flux (weber)" },
    { 0x272D,   "magnetic flux density (tesla)" },
    { 0x272E,   "inductance (henry)" },
    { 0x272F,   "Celsius temperature (degree Celsius)" },
    { 0x2730,   "luminous flux (lumen)" },
    { 0x2731,   "illuminance (lux)" },
    { 0x2732,   "activity referred to a radionuclide (becquerel)" },
    { 0x2733,   "absorbed dose (gray)" },
    { 0x2734,   "dose equivalent (sievert)" },
    { 0x2735,   "catalytic activity (katal)" },
    { 0x2740,   "dynamic viscosity (pascal second)" },
    { 0x2741,   "moment of force (newton metre)" },
    { 0x2742,   "surface tension (newton per metre)" },
    { 0x2743,   "angular velocity (radian per second)" },
    { 0x2744,   "angular acceleration (radian per second squared)" },
    { 0x2745,   "heat flux density (watt per square metre)" },
    { 0x2746,   "heat capacity (joule per kelvin)" },
    { 0x2747,   "specific heat capacity (joule per kilogram kelvin)" },
    { 0x2748,   "specific energy (joule per kilogram)" },
    { 0x2749,   "thermal conductivity (watt per metre kelvin)" },
    { 0x274A,   "energy density (joule per cubic metre)" },
    { 0x274B,   "electric field strength (volt per metre)" },
    { 0x274C,   "electric charge density (coulomb per cubic metre)" },
    { 0x274D,   "surface charge density (coulomb per square metre)" },
    { 0x274E,   "electric flux density (coulomb per square metre)" },
    { 0x274F,   "permittivity (farad per metre)" },
    { 0x2750,   "permeability (henry per metre)" },
    { 0x2751,   "molar energy (joule per mole)" },
    { 0x2752,   "molar entropy (joule per mole kelvin)" },
    { 0x2753,   "exposure (coulomb per kilogram)" },
    { 0x2754,   "absorbed dose rate (gray per second)" },
    { 0x2755,   "radiant intensity (watt per steradian)" },
    { 0x2756,   "radiance (watt per square metre steradian)" },
    { 0x2757,   "catalytic activity concentration (katal per cubic metre)" },
    { 0x2760,   "time (minute)" },
    { 0x2761,   "time (hour)" },
    { 0x2762,   "time (day)" },
    { 0x2763,   "plane angle (degree)" },
    { 0x2764,   "plane angle (minute)" },
    { 0x2765,   "plane angle (second)" },
    { 0x2766,   "area (hectare)" },
    { 0x2767,   "volume (litre)" },
    { 0x2768,   "mass (tonne)" },
    { 0x2780,   "pressure (bar)" },
    { 0x2781,   "pressure (millimetre of mercury)" },
    { 0x2782,   "length (ångström)" },
    { 0x2783,   "length (nautical mile)" },
    { 0x2784,   "area (barn)" },
    { 0x2785,   "velocity (knot)" },
    { 0x2786,   "logarithmic radio quantity (neper)" },
    { 0x2787,   "logarithmic radio quantity (bel)" },
    { 0x27A0,   "length (yard)" },
    { 0x27A1,   "length (parsec)" },
    { 0x27A2,   "length (inch)" },
    { 0x27A3,   "length (foot)" },
    { 0x27A4,   "length (mile)" },
    { 0x27A5,   "pressure (pound-force per square inch)" },
    { 0x27A6,   "velocity (kilometre per hour)" },
    { 0x27A7,   "velocity (mile per hour)" },
    { 0x27A8,   "angular velocity (revolution per minute)" },
    { 0x27A9,   "energy (gram calorie)" },
    { 0x27AA,   "energy (kilogram calorie)" },
    { 0x27AB,   "energy (kilowatt hour)" },
    { 0x27AC,   "thermodynamic temperature (degree Fahrenheit)" },
    { 0x27AD,   "percentage" },
    { 0x27AE,   "per mille" },
    { 0x27AF,   "period (beats per minute)" },
    { 0x27B0,   "electric charge (ampere hours)" },
    { 0x27B1,   "mass density (milligram per decilitre)" },
    { 0x27B2,   "mass density (millimole per litre)" },
    { 0x27B3,   "time (year)" },
    { 0x27B4,   "time (month)" },
    { 0x27B5,   "concentration (count per cubic metre)" },
    { 0x27B6,   "irradiance (watt per square metre)" },
    { 0x27B7,   "milliliter (per kilogram per minute)" },
    { 0x27B8,   "mass (pound)" },
    { 0x27B9,   "metabolic equivalent" },
    { 0x27BA,   "step (per minute)" },
    { 0x27BC,   "stroke (per minute)" },
    { 0x27BD,   "pace (kilometre per minute)" },
    { 0x27BE,   "luminous efficacy (lumen per watt)" },
    { 0x27BF,   "luminous energy (lumen hour)" },
    { 0x27C0,   "luminous exposure (lux hour)" },
    { 0x27C1,   "mass flow (gram per second)" },
    { 0x27C2,   "volume flow (litre per second)" },
    { 0x27C3,   "sound pressure (decibel)" },
    { 0x27C4,   "parts per million" },
    { 0x27C5,   "parts per billion" },
    { 0x27C6,   "mass density rate ((milligram per decilitre) per minute)" },
    { 0x27C7,   "Electrical Apparent Energy (kilovolt ampere hour)" },
    { 0x27C8,   "Electrical Apparent Power (volt ampere)" },
    /* Declarations - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/declarations.yaml */
    { 0x2800,   "Primary Service" },
    { 0x2801,   "Secondary Service" },
    { 0x2802,   "Include" },
    { 0x2803,   "Characteristic" },
    /* Descriptors - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/descriptors.yaml */
    { 0x2900,   "Characteristic Extended Properties" },
    { 0x2901,   "Characteristic User Description" },
    { 0x2902,   "Client Characteristic Configuration" },
    { 0x2903,   "Server Characteristic Configuration" },
    { 0x2904,   "Characteristic Presentation Format" },
    { 0x2905,   "Characteristic Aggregate Format" },
    { 0x2906,   "Valid Range" },
    { 0x2907,   "External Report Reference" },
    { 0x2908,   "Report Reference" },
    { 0x2909,   "Number of Digitals" },
    { 0x290A,   "Value Trigger Setting" },
    { 0x290B,   "Environmental Sensing Configuration" },
    { 0x290C,   "Environmental Sensing Measurement" },
    { 0x290D,   "Environmental Sensing Trigger Setting" },
    { 0x290E,   "Time Trigger Setting" },
    { 0x290F,   "Complete BR-EDR Transport Block Data" },
    { 0x2910,   "Observation Schedule" },
    { 0x2911,   "Valid Range and Accuracy" },
    /* Characteristics - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/characteristic_uuids.yaml */
    { 0x2A00,   "Device Name" },
    { 0x2A01,   "Appearance" },
    { 0x2A02,   "Peripheral Privacy Flag" },
    { 0x2A03,   "Reconnection Address" },
    { 0x2A04,   "Peripheral Preferred Connection Parameters" },
    { 0x2A05,   "Service Changed" },
    { 0x2A06,   "Alert Level" },
    { 0x2A07,   "Tx Power Level" },
    { 0x2A08,   "Date Time" },
    { 0x2A09,   "Day of Week" },
    { 0x2A0A,   "Day Date Time" },
    { 0x2A0B,   "Exact Time 100" },
    { 0x2A0C,   "Exact Time 256" },
    { 0x2A0D,   "DST Offset" },
    { 0x2A0E,   "Time Zone" },
    { 0x2A0F,   "Local Time Information" },
    { 0x2A10,   "Secondary Time Zone" },
    { 0x2A11,   "Time with DST" },
    { 0x2A12,   "Time Accuracy" },
    { 0x2A13,   "Time Source" },
    { 0x2A14,   "Reference Time Information" },
    { 0x2A15,   "Time Broadcast" },
    { 0x2A16,   "Time Update Control Point" },
    { 0x2A17,   "Time Update State" },
    { 0x2A18,   "Glucose Measurement" },
    { 0x2A19,   "Battery Level" },
    { 0x2A1A,   "Battery Power State" },
    { 0x2A1B,   "Battery Level State" },
    { 0x2A1C,   "Temperature Measurement" },
    { 0x2A1D,   "Temperature Type" },
    { 0x2A1E,   "Intermediate Temperature" },
    { 0x2A1F,   "Temperature Celsius" },
    { 0x2A20,   "Temperature Fahrenheit" },
    { 0x2A21,   "Measurement Interval" },
    { 0x2A22,   "Boot Keyboard Input Report" },
    { 0x2A23,   "System ID" },
    { 0x2A24,   "Model Number String" },
    { 0x2A25,   "Serial Number String" },
    { 0x2A26,   "Firmware Revision String" },
    { 0x2A27,   "Hardware Revision String" },
    { 0x2A28,   "Software Revision String" },
    { 0x2A29,   "Manufacturer Name String" },
    { 0x2A2A,   "IEEE 11073-20601 Regulatory Certification Data List" },
    { 0x2A2B,   "Current Time" },
    { 0x2A2C,   "Magnetic Declination" },
    { 0x2A2F,   "Position 2D" },
    { 0x2A30,   "Position 3D" },
    { 0x2A31,   "Scan Refresh" },
    { 0x2A32,   "Boot Keyboard Output Report" },
    { 0x2A33,   "Boot Mouse Input Report" },
    { 0x2A34,   "Glucose Measurement Context" },
    { 0x2A35,   "Blood Pressure Measurement" },
    { 0x2A36,   "Intermediate Cuff Pressure" },
    { 0x2A37,   "Heart Rate Measurement" },
    { 0x2A38,   "Body Sensor Location" },
    { 0x2A39,   "Heart Rate Control Point" },
    { 0x2A3A,   "Removable" },
    { 0x2A3B,   "Service Required" },
    { 0x2A3C,   "Scientific Temperature Celsius" },
    { 0x2A3D,   "String" },
    { 0x2A3E,   "Network Availability" },
    { 0x2A3F,   "Alert Status" },
    { 0x2A40,   "Ringer Control Point" },
    { 0x2A41,   "Ringer Setting" },
    { 0x2A42,   "Alert Category ID Bit Mask" },
    { 0x2A43,   "Alert Category ID" },
    { 0x2A44,   "Alert Notification Control Point" },
    { 0x2A45,   "Unread Alert Status" },
    { 0x2A46,   "New Alert" },
    { 0x2A47,   "Supported New Alert Category" },
    { 0x2A48,   "Supported Unread Alert Category" },
    { 0x2A49,   "Blood Pressure Feature" },
    { 0x2A4A,   "HID Information" },
    { 0x2A4B,   "Report Map" },
    { 0x2A4C,   "HID Control Point" },
    { 0x2A4D,   "Report" },
    { 0x2A4E,   "Protocol Mode" },
    { 0x2A4F,   "Scan Interval Window" },
    { 0x2A50,   "PnP ID" },
    { 0x2A51,   "Glucose Feature" },
    { 0x2A52,   "Record Access Control Point" },
    { 0x2A53,   "RSC Measurement" },
    { 0x2A54,   "RSC Feature" },
    { 0x2A55,   "SC Control Point" },
    { 0x2A56,   "Digital" },
    { 0x2A57,   "Digital Output" },
    { 0x2A58,   "Analog" },
    { 0x2A59,   "Analog Output" },
    { 0x2A5A,   "Aggregate" },
    { 0x2A5B,   "CSC Measurement" },
    { 0x2A5C,   "CSC Feature" },
    { 0x2A5D,   "Sensor Location" },
    { 0x2A5E,   "PLX Spot-Check Measurement" },
    { 0x2A5F,   "PLX Continuous Measurement" },
    { 0x2A60,   "PLX Features" },
    { 0x2A62,   "Pulse Oximetry Control Point" },
    { 0x2A63,   "Cycling Power Measurement" },
    { 0x2A64,   "Cycling Power Vector" },
    { 0x2A65,   "Cycling Power Feature" },
    { 0x2A66,   "Cycling Power Control Point" },
    { 0x2A67,   "Location and Speed" },
    { 0x2A68,   "Navigation" },
    { 0x2A69,   "Position Quality" },
    { 0x2A6A,   "LN Feature" },
    { 0x2A6B,   "LN Control Point" },
    { 0x2A6C,   "Elevation" },
    { 0x2A6D,   "Pressure" },
    { 0x2A6E,   "Temperature" },
    { 0x2A6F,   "Humidity" },
    { 0x2A70,   "True Wind Speed" },
    { 0x2A71,   "True Wind Direction" },
    { 0x2A72,   "Apparent Wind Speed" },
    { 0x2A73,   "Apparent Wind Direction" },
    { 0x2A74,   "Gust Factor" },
    { 0x2A75,   "Pollen Concentration" },
    { 0x2A76,   "UV Index" },
    { 0x2A77,   "Irradiance" },
    { 0x2A78,   "Rainfall" },
    { 0x2A79,   "Wind Chill" },
    { 0x2A7A,   "Heat Index" },
    { 0x2A7B,   "Dew Point" },
    { 0x2A7D,   "Descriptor Value Changed" },
    { 0x2A7E,   "Aerobic Heart Rate Lower Limit" },
    { 0x2A7F,   "Aerobic Threshold" },
    { 0x2A80,   "Age" },
    { 0x2A81,   "Anaerobic Heart Rate Lower Limit" },
    { 0x2A82,   "Anaerobic Heart Rate Upper Limit" },
    { 0x2A83,   "Anaerobic Threshold" },
    { 0x2A84,   "Aerobic Heart Rate Upper Limit" },
    { 0x2A85,   "Date of Birth" },
    { 0x2A86,   "Date of Threshold Assessment" },
    { 0x2A87,   "Email Address" },
    { 0x2A88,   "Fat Burn Heart Rate Lower Limit" },
    { 0x2A89,   "Fat Burn Heart Rate Upper Limit" },
    { 0x2A8A,   "First Name" },
    { 0x2A8B,   "Five Zone Heart Rate Limits" },
    { 0x2A8C,   "Gender" },
    { 0x2A8D,   "Heart Rate Max" },
    { 0x2A8E,   "Height" },
    { 0x2A8F,   "Hip Circumference" },
    { 0x2A90,   "Last Name" },
    { 0x2A91,   "Maximum Recommended Heart Rate" },
    { 0x2A92,   "Resting Heart Rate" },
    { 0x2A93,   "Sport Type for Aerobic and Anaerobic Thresholds" },
    { 0x2A94,   "Three Zone Heart Rate Limits" },
    { 0x2A95,   "Two Zone Heart Rate Limits" },
    { 0x2A96,   "VO2 Max" },
    { 0x2A97,   "Waist Circumference" },
    { 0x2A98,   "Weight" },
    { 0x2A99,   "Database Change Increment" },
    { 0x2A9A,   "User Index" },
    { 0x2A9B,   "Body Composition Feature" },
    { 0x2A9C,   "Body Composition Measurement" },
    { 0x2A9D,   "Weight Measurement" },
    { 0x2A9E,   "Weight Scale Feature" },
    { 0x2A9F,   "User Control Point" },
    { 0x2AA0,   "Magnetic Flux Density - 2D" },
    { 0x2AA1,   "Magnetic Flux Density - 3D" },
    { 0x2AA2,   "Language" },
    { 0x2AA3,   "Barometric Pressure Trend" },
    { 0x2AA4,   "Bond Management Control Point" },
    { 0x2AA5,   "Bond Management Feature" },
    { 0x2AA6,   "Central Address Resolution" },
    { 0x2AA7,   "CGM Measurement" },
    { 0x2AA8,   "CGM Feature" },
    { 0x2AA9,   "CGM Status" },
    { 0x2AAA,   "CGM Session Start Time" },
    { 0x2AAB,   "CGM Session Run Time" },
    { 0x2AAC,   "CGM Specific Ops Control Point" },
    { 0x2AAD,   "Indoor Positioning Configuration" },
    { 0x2AAE,   "Latitude" },
    { 0x2AAF,   "Longitude" },
    { 0x2AB0,   "Local North Coordinate" },
    { 0x2AB1,   "Local East Coordinate" },
    { 0x2AB2,   "Floor Number" },
    { 0x2AB3,   "Altitude" },
    { 0x2AB4,   "Uncertainty" },
    { 0x2AB5,   "Location Name" },
    { 0x2AB6,   "URI" },
    { 0x2AB7,   "HTTP Headers" },
    { 0x2AB8,   "HTTP Status Code" },
    { 0x2AB9,   "HTTP Entity Body" },
    { 0x2ABA,   "HTTP Control Point" },
    { 0x2ABB,   "HTTPS Security" },
    { 0x2ABC,   "TDS Control Point" },
    { 0x2ABD,   "OTS Feature" },
    { 0x2ABE,   "Object Name" },
    { 0x2ABF,   "Object Type" },
    { 0x2AC0,   "Object Size" },
    { 0x2AC1,   "Object First-Created" },
    { 0x2AC2,   "Object Last-Modified" },
    { 0x2AC3,   "Object ID" },
    { 0x2AC4,   "Object Properties" },
    { 0x2AC5,   "Object Action Control Point" },
    { 0x2AC6,   "Object List Control Point" },
    { 0x2AC7,   "Object List Filter" },
    { 0x2AC8,   "Object Changed" },
    { 0x2AC9,   "Resolvable Private Address Only" },
    { 0x2ACA,   "Unspecified" },
    { 0x2ACB,   "Directory Listing" },
    { 0x2ACC,   "Fitness Machine Feature" },
    { 0x2ACD,   "Treadmill Data" },
    { 0x2ACE,   "Cross Trainer Data" },
    { 0x2ACF,   "Step Climber Data" },
    { 0x2AD0,   "Stair Climber Data" },
    { 0x2AD1,   "Rower Data" },
    { 0x2AD2,   "Indoor Bike Data" },
    { 0x2AD3,   "Training Status" },
    { 0x2AD4,   "Supported Speed Range" },
    { 0x2AD5,   "Supported Inclination Range" },
    { 0x2AD6,   "Supported Resistance Level Range" },
    { 0x2AD7,   "Supported Heart Rate Range" },
    { 0x2AD8,   "Supported Power Range" },
    { 0x2AD9,   "Fitness Machine Control Point" },
    { 0x2ADA,   "Fitness Machine Status" },
    { 0x2ADB,   "Mesh Provisioning Data In" },
    { 0x2ADC,   "Mesh Provisioning Data Out" },
    { 0x2ADD,   "Mesh Proxy Data In" },
    { 0x2ADE,   "Mesh Proxy Data Out" },
    { 0x2AE0,   "Average Current" },
    { 0x2AE1,   "Average Voltage" },
    { 0x2AE2,   "Boolean" },
    { 0x2AE3,   "Chromatic Distance from Planckian" },
    { 0x2AE4,   "Chromaticity Coordinates" },
    { 0x2AE5,   "Chromaticity in CCT and Duv Values" },
    { 0x2AE6,   "Chromaticity Tolerance" },
    { 0x2AE7,   "CIE 13.3-1995 Color Rendering Index" },
    { 0x2AE8,   "Coefficient" },
    { 0x2AE9,   "Correlated Color Temperature" },
    { 0x2AEA,   "Count 16" },
    { 0x2AEB,   "Count 24" },
    { 0x2AEC,   "Country Code" },
    { 0x2AED,   "Date UTC" },
    { 0x2AEE,   "Electric Current" },
    { 0x2AEF,   "Electric Current Range" },
    { 0x2AF0,   "Electric Current Specification" },
    { 0x2AF1,   "Electric Current Statistics" },
    { 0x2AF2,   "Energy" },
    { 0x2AF3,   "Energy in a Period of Day" },
    { 0x2AF4,   "Event Statistics" },
    { 0x2AF5,   "Fixed String 16" },
    { 0x2AF6,   "Fixed String 24" },
    { 0x2AF7,   "Fixed String 36" },
    { 0x2AF8,   "Fixed String 8" },
    { 0x2AF9,   "Generic Level" },
    { 0x2AFA,   "Global Trade Item Number" },
    { 0x2AFB,   "Illuminance" },
    { 0x2AFC,   "Luminous Efficacy" },
    { 0x2AFD,   "Luminous Energy" },
    { 0x2AFE,   "Luminous Exposure" },
    { 0x2AFF,   "Luminous Flux" },
    { 0x2B00,   "Luminous Flux Range" },
    { 0x2B01,   "Luminous Intensity" },
    { 0x2B02,   "Mass Flow" },
    { 0x2B03,   "Perceived Lightness" },
    { 0x2B04,   "Percentage 8" },
    { 0x2B05,   "Power" },
    { 0x2B06,   "Power Specification" },
    { 0x2B07,   "Relative Runtime in a Current Range" },
    { 0x2B08,   "Relative Runtime in a Generic Level Range" },
    { 0x2B09,   "Relative Value in a Voltage Range" },
    { 0x2B0A,   "Relative Value in an Illuminance Range" },
    { 0x2B0B,   "Relative Value in a Period of Day" },
    { 0x2B0C,   "Relative Value in a Temperature Range" },
    { 0x2B0D,   "Temperature 8" },
    { 0x2B0E,   "Temperature 8 in a Period of Day" },
    { 0x2B0F,   "Temperature 8 Statistics" },
    { 0x2B10,   "Temperature Range" },
    { 0x2B11,   "Temperature Statistics" },
    { 0x2B12,   "Time Decihour 8" },
    { 0x2B13,   "Time Exponential 8" },
    { 0x2B14,   "Time Hour 24" },
    { 0x2B15,   "Time Millisecond 24" },
    { 0x2B16,   "Time Second 16" },
    { 0x2B17,   "Time Second 8" },
    { 0x2B18,   "Voltage" },
    { 0x2B19,   "Voltage Specification" },
    { 0x2B1A,   "Voltage Statistics" },
    { 0x2B1B,   "Volume Flow" },
    { 0x2B1C,   "Chromaticity Coordinate" },
    { 0x2B1D,   "RC Feature" },
    { 0x2B1E,   "RC Settings" },
    { 0x2B1F,   "Reconnection Configuration Control Point" },
    { 0x2B20,   "IDD Status Changed" },
    { 0x2B21,   "IDD Status" },
    { 0x2B22,   "IDD Annunciation Status" },
    { 0x2B23,   "IDD Features" },
    { 0x2B24,   "IDD Status Reader Control Point" },
    { 0x2B25,   "IDD Command Control Point" },
    { 0x2B26,   "IDD Command Data" },
    { 0x2B27,   "IDD Record Access Control Point" },
    { 0x2B28,   "IDD History Data" },
    { 0x2B29,   "Client Supported Features" },
    { 0x2B2A,   "Database Hash" },
    { 0x2B2B,   "BSS Control Point" },
    { 0x2B2C,   "BSS Response" },
    { 0x2B2D,   "Emergency ID" },
    { 0x2B2E,   "Emergency Text" },
    { 0x2B2F,   "ACS Status" },
    { 0x2B30,   "ACS Data In" },
    { 0x2B31,   "ACS Data Out Notify" },
    { 0x2B32,   "ACS Data Out Indicate" },
    { 0x2B33,   "ACS Control Point" },
    { 0x2B34,   "Enhanced Blood Pressure Measurement" },
    { 0x2B35,   "Enhanced Intermediate Cuff Pressure" },
    { 0x2B36,   "Blood Pressure Record" },
    { 0x2B37,   "Registered User" },
    { 0x2B38,   "BR-EDR Handover Data" },
    { 0x2B39,   "Bluetooth SIG Data" },
    { 0x2B3A,   "Server Supported Features" },
    { 0x2B3B,   "Physical Activity Monitor Features" },
    { 0x2B3C,   "General Activity Instantaneous Data" },
    { 0x2B3D,   "General Activity Summary Data" },
    { 0x2B3E,   "CardioRespiratory Activity Instantaneous Data" },
    { 0x2B3F,   "CardioRespiratory Activity Summary Data" },
    { 0x2B40,   "Step Counter Activity Summary Data" },
    { 0x2B41,   "Sleep Activity Instantaneous Data" },
    { 0x2B42,   "Sleep Activity Summary Data" },
    { 0x2B43,   "Physical Activity Monitor Control Point" },
    { 0x2B44,   "Physical Activity Current Session" },
    { 0x2B45,   "Physical Activity Session Descriptor" },
    { 0x2B46,   "Preferred Units" },
    { 0x2B47,   "High Resolution Height" },
    { 0x2B48,   "Middle Name" },
    { 0x2B49,   "Stride Length" },
    { 0x2B4A,   "Handedness" },
    { 0x2B4B,   "Device Wearing Position" },
    { 0x2B4C,   "Four Zone Heart Rate Limits" },
    { 0x2B4D,   "High Intensity Exercise Threshold" },
    { 0x2B4E,   "Activity Goal" },
    { 0x2B4F,   "Sedentary Interval Notification" },
    { 0x2B50,   "Caloric Intake" },
    { 0x2B51,   "TMAP Role" },
    { 0x2B77,   "Audio Input State" },
    { 0x2B78,   "Gain Settings Attribute" },
    { 0x2B79,   "Audio Input Type" },
    { 0x2B7A,   "Audio Input Status" },
    { 0x2B7B,   "Audio Input Control Point" },
    { 0x2B7C,   "Audio Input Description" },
    { 0x2B7D,   "Volume State" },
    { 0x2B7E,   "Volume Control Point" },
    { 0x2B7F,   "Volume Flags" },
    { 0x2B80,   "Volume Offset State" },
    { 0x2B81,   "Audio Location" },
    { 0x2B82,   "Volume Offset Control Point" },
    { 0x2B83,   "Audio Output Description" },
    { 0x2B84,   "Set Identity Resolving Key" },
    { 0x2B85,   "Coordinated Set Size" },
    { 0x2B86,   "Set Member Lock" },
    { 0x2B87,   "Set Member Rank" },
    { 0x2B88,   "Encrypted Data Key Material" },
    { 0x2B89,   "Apparent Energy 32" },
    { 0x2B8A,   "Apparent Power" },
    { 0x2B8B,   "Live Health Observations" },
    { 0x2B8C,   "CO₂ Concentration" },
    { 0x2B8D,   "Cosine of the Angle" },
    { 0x2B8E,   "Device Time Feature" },
    { 0x2B8F,   "Device Time Parameters" },
    { 0x2B90,   "Device Time" },
    { 0x2B91,   "Device Time Control Point" },
    { 0x2B92,   "Time Change Log Data" },
    { 0x2B93,   "Media Player Name" },
    { 0x2B94,   "Media Player Icon Object ID" },
    { 0x2B95,   "Media Player Icon URL" },
    { 0x2B96,   "Track Changed" },
    { 0x2B97,   "Track Title" },
    { 0x2B98,   "Track Duration" },
    { 0x2B99,   "Track Position" },
    { 0x2B9A,   "Playback Speed" },
    { 0x2B9B,   "Seeking Speed" },
    { 0x2B9C,   "Current Track Segments Object ID" },
    { 0x2B9D,   "Current Track Object ID" },
    { 0x2B9E,   "Next Track Object ID" },
    { 0x2B9F,   "Parent Group Object ID" },
    { 0x2BA0,   "Current Group Object ID" },
    { 0x2BA1,   "Playing Order" },
    { 0x2BA2,   "Playing Orders Supported" },
    { 0x2BA3,   "Media State" },
    { 0x2BA4,   "Media Control Point" },
    { 0x2BA5,   "Media Control Point Opcodes Supported" },
    { 0x2BA6,   "Search Results Object ID" },
    { 0x2BA7,   "Search Control Point" },
    { 0x2BA8,   "Energy 32" },
    { 0x2BA9,   "Media Player Icon Object Type" },
    { 0x2BAA,   "Track Segments Object Type" },
    { 0x2BAB,   "Track Object Type" },
    { 0x2BAC,   "Group Object Type" },
    { 0x2BAD,   "Constant Tone Extension Enable" },
    { 0x2BAE,   "Advertising Constant Tone Extension Minimum Length" },
    { 0x2BAF,   "Advertising Constant Tone Extension Minimum Transmit Count" },
    { 0x2BB0,   "Advertising Constant Tone Extension Transmit Duration" },
    { 0x2BB1,   "Advertising Constant Tone Extension Interval" },
    { 0x2BB2,   "Advertising Constant Tone Extension PHY" },
    { 0x2BB3,   "Bearer Provider Name" },
    { 0x2BB4,   "Bearer UCI" },
    { 0x2BB5,   "Bearer Technology" },
    { 0x2BB6,   "Bearer URI Schemes Supported List" },
    { 0x2BB7,   "Bearer Signal Strength" },
    { 0x2BB8,   "Bearer Signal Strength Reporting Interval" },
    { 0x2BB9,   "Bearer List Current Calls" },
    { 0x2BBA,   "Content Control ID" },
    { 0x2BBB,   "Status Flags" },
    { 0x2BBC,   "Incoming Call Target Bearer URI" },
    { 0x2BBD,   "Call State" },
    { 0x2BBE,   "Call Control Point" },
    { 0x2BBF,   "Call Control Point Optional Opcodes" },
    { 0x2BC0,   "Termination Reason" },
    { 0x2BC1,   "Incoming Call" },
    { 0x2BC2,   "Call Friendly Name" },
    { 0x2BC3,   "Mute" },
    { 0x2BC4,   "Sink ASE" },
    { 0x2BC5,   "Source ASE" },
    { 0x2BC6,   "ASE Control Point" },
    { 0x2BC7,   "Broadcast Audio Scan Control Point" },
    { 0x2BC8,   "Broadcast Receive State" },
    { 0x2BC9,   "Sink PAC" },
    { 0x2BCA,   "Sink Audio Locations" },
    { 0x2BCB,   "Source PAC" },
    { 0x2BCC,   "Source Audio Locations" },
    { 0x2BCD,   "Available Audio Contexts" },
    { 0x2BCE,   "Supported Audio Contexts" },
    { 0x2BCF,   "Ammonia Concentration" },
    { 0x2BD0,   "Carbon Monoxide Concentration" },
    { 0x2BD1,   "Methane Concentration" },
    { 0x2BD2,   "Nitrogen Dioxide Concentration" },
    { 0x2BD3,   "Non-Methane Volatile Organic Compounds Concentration" },
    { 0x2BD4,   "Ozone Concentration" },
    { 0x2BD5,   "Particulate Matter - PM1 Concentration" },
    { 0x2BD6,   "Particulate Matter - PM2.5 Concentration" },
    { 0x2BD7,   "Particulate Matter - PM10 Concentration" },
    { 0x2BD8,   "Sulfur Dioxide Concentration" },
    { 0x2BD9,   "Sulfur Hexafluoride Concentration" },
    { 0x2BDA,   "Hearing Aid Features" },
    { 0x2BDB,   "Hearing Aid Preset Control Point" },
    { 0x2BDC,   "Active Preset Index" },
    { 0x2BDD,   "Stored Health Observations" },
    { 0x2BDE,   "Fixed String 64" },
    { 0x2BDF,   "High Temperature" },
    { 0x2BE0,   "High Voltage" },
    { 0x2BE1,   "Light Distribution" },
    { 0x2BE2,   "Light Output" },
    { 0x2BE3,   "Light Source Type" },
    { 0x2BE4,   "Noise" },
    { 0x2BE5,   "Relative Runtime in a Correlated Color Temperature Range" },
    { 0x2BE6,   "Time Second 32" },
    { 0x2BE7,   "VOC Concentration" },
    { 0x2BE8,   "Voltage Frequency" },
    { 0x2BE9,   "Battery Critical Status" },
    { 0x2BEA,   "Battery Health Status" },
    { 0x2BEB,   "Battery Health Information" },
    { 0x2BEC,   "Battery Information" },
    { 0x2BED,   "Battery Level Status" },
    { 0x2BEE,   "Battery Time Status" },
    { 0x2BEF,   "Estimated Service Date" },
    { 0x2BF0,   "Battery Energy Status" },
    { 0x2BF1,   "Observation Schedule Changed" },
    { 0x2BF2,   "Current Elapsed Time" },
    { 0x2BF3,   "Health Sensor Features" },
    { 0x2BF4,   "GHS Control Point" },
    { 0x2BF5,   "LE GATT Security Levels" },
    { 0x2BF6,   "ESL Address" },
    { 0x2BF7,   "AP Sync Key Material" },
    { 0x2BF8,   "ESL Response Key Material" },
    { 0x2BF9,   "ESL Current Absolute Time" },
    { 0x2BFA,   "ESL Display Information" },
    { 0x2BFB,   "ESL Image Information" },
    { 0x2BFC,   "ESL Sensor Information" },
    { 0x2BFD,   "ESL LED Information" },
    { 0x2BFE,   "ESL Control Point" },
    { 0x2BFF,   "UDI for Medical Devices" },
    { 0x2C00,   "GMAP Role" },
    { 0x2C01,   "UGG Features" },
    { 0x2C02,   "UGT Features" },
    { 0x2C03,   "BGS Features" },
    { 0x2C04,   "BGR Features" },
    { 0x2C05,   "Percentage 8 Steps" },
    /* Members - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/member_uuids.yaml */
    { 0xFC79,   "LG Electronics Inc." },
    { 0xFC7A,   "Outshiny India Private Limited" },
    { 0xFC7B,   "Testo SE & Co. KGaA" },
    { 0xFC7C,   "Motorola Mobility, LLC" },
    { 0xFC7D,   "MML US, Inc" },
    { 0xFC7E,   "Harman International" },
    { 0xFC7F,   "Southco" },
    { 0xFC80,   "TELE System Communications Pte. Ltd." },
    { 0xFC81,   "Axon Enterprise, Inc." },
    { 0xFC82,   "Zwift, Inc." },
    { 0xFC83,   "iHealth Labs, Inc." },
    { 0xFC84,   "NINGBO FOTILE KITCHENWARE CO., LTD." },
    { 0xFC85,   "Zhejiang Huanfu Technology Co., LTD" },
    { 0xFC86,   "Samsara Networks, Inc" },
    { 0xFC87,   "Samsara Networks, Inc" },
    { 0xFC88,   "CCC del Uruguay" },
    { 0xFC89,   "Intel Corporation" },
    { 0xFC8A,   "Intel Corporation" },
    { 0xFC8B,   "Kaspersky Lab Middle East FZ-LLC" },
    { 0xFC8C,   "SES-Imagotag" },
    { 0xFC8D,   "Caire Inc." },
    { 0xFC8E,   "Blue Iris Labs, Inc." },
    { 0xFC8F,   "Bose Corporation" },
    { 0xFC90,   "Wiliot LTD." },
    { 0xFC91,   "Samsung Electronics Co., Ltd." },
    { 0xFC92,   "Furuno Electric Co., Ltd." },
    { 0xFC93,   "Komatsu Ltd." },
    { 0xFC94,   "Apple Inc." },
    { 0xFC95,   "Hippo Camp Software Ltd." },
    { 0xFC96,   "LEGO System A/S" },
    { 0xFC97,   "Japan Display Inc." },
    { 0xFC98,   "Ruuvi Innovations Ltd." },
    { 0xFC99,   "Badger Meter" },
    { 0xFC9A,   "Plockat Solutions AB" },
    { 0xFC9B,   "Merry Electronics (S) Pte Ltd" },
    { 0xFC9C,   "Binary Power, Inc." },
    { 0xFC9D,   "Lenovo (Singapore) Pte Ltd." },
    { 0xFC9E,   "Dell Computer Corporation" },
    { 0xFC9F,   "Delta Development Team, Inc" },
    { 0xFCA0,   "Apple Inc." },
    { 0xFCA1,   "PF SCHWEISSTECHNOLOGIE GMBH" },
    { 0xFCA2,   "Meizu Technology Co., Ltd." },
    { 0xFCA3,   "Gunnebo Aktiebolag" },
    { 0xFCA4,   "HP Inc." },
    { 0xFCA5,   "HAYWARD INDUSTRIES, INC." },
    { 0xFCA6,   "Hubble Network Inc." },
    { 0xFCA7,   "Hubble Network Inc." },
    { 0xFCA8,   "Medtronic Inc." },
    { 0xFCA9,   "Medtronic Inc." },
    { 0xFCAA,   "Spintly, Inc." },
    { 0xFCAB,   "IRISS INC." },
    { 0xFCAC,   "IRISS INC." },
    { 0xFCAD,   "Beijing 99help Safety Technology Co., Ltd" },
    { 0xFCAE,   "Imagine Marketing Limited" },
    { 0xFCAF,   "AltoBeam Inc." },
    { 0xFCB0,   "Ford Motor Company" },
    { 0xFCB1,   "Google LLC" },
    { 0xFCB2,   "Apple Inc." },
    { 0xFCB3,   "SWEEN" },
    { 0xFCB4,   "OMRON HEALTHCARE Co., Ltd." },
    { 0xFCB5,   "OMRON HEALTHCARE Co., Ltd." },
    { 0xFCB6,   "OMRON HEALTHCARE Co., Ltd." },
    { 0xFCB7,   "T-Mobile USA" },
    { 0xFCB8,   "Ribbiot, INC." },
    { 0xFCB9,   "Lumi United Technology Co., Ltd" },
    { 0xFCBA,   "BlueID GmbH" },
    { 0xFCBB,   "SharkNinja Operating LLC" },
    { 0xFCBC,   "Drowsy Digital, Inc." },
    { 0xFCBD,   "Toshiba Corporation" },
    { 0xFCBE,   "Musen Connect, Inc." },
    { 0xFCBF,   "ASSA ABLOY Opening Solutions Sweden AB" },
    { 0xFCC0,   "Xiaomi Inc." },
    { 0xFCC1,   "TIMECODE SYSTEMS LIMITED" },
    { 0xFCC2,   "Qualcomm Technologies, Inc." },
    { 0xFCC3,   "HP Inc." },
    { 0xFCC4,   "OMRON(DALIAN) CO,.LTD." },
    { 0xFCC5,   "OMRON(DALIAN) CO,.LTD." },
    { 0xFCC6,   "Wiliot LTD." },
    { 0xFCC7,   "PB INC." },
    { 0xFCC8,   "Allthenticate, Inc." },
    { 0xFCC9,   "SkyHawke Technologies" },
    { 0xFCCA,   "Cosmed s.r.l." },
    { 0xFCCB,   "TOTO LTD." },
    { 0xFCCC,   "Wi-Fi Easy Connect Specification" },
    { 0xFCCD,   "Zound Industries International AB" },
    { 0xFCCE,   "Luna Health, Inc." },
    { 0xFCCF,   "Google LLC" },
    { 0xFCD0,   "Laerdal Medical AS" },
    { 0xFCD1,   "Shenzhen Benwei Media Co.,Ltd." },
    { 0xFCD2,   "Allterco Robotics ltd" },
    { 0xFCD3,   "Fisher & Paykel Healthcare" },
    { 0xFCD4,   "OMRON HEALTHCARE" },
    { 0xFCD5,   "Nortek Security & Control" },
    { 0xFCD6,   "SWISSINNO SOLUTIONS AG" },
    { 0xFCD7,   "PowerPal Pty Ltd" },
    { 0xFCD8,   "Appex Factory S.L." },
    { 0xFCD9,   "Huso, INC" },
    { 0xFCDA,   "Draeger" },
    { 0xFCDB,   "aconno GmbH" },
    { 0xFCDC,   "Amazon.com Services, LLC" },
    { 0xFCDD,   "Mobilaris AB" },
    { 0xFCDE,   "ARCTOP, INC." },
    { 0xFCDF,   "NIO USA, Inc." },
    { 0xFCE0,   "Akciju sabiedriba \"SAF TEHNIKA\"" },
    { 0xFCE1,   "Sony Group Corporation" },
    { 0xFCE2,   "Baracoda Daily Healthtech" },
    { 0xFCE3,   "Smith & Nephew Medical Limited" },
    { 0xFCE4,   "Samsara Networks, Inc" },
    { 0xFCE5,   "Samsara Networks, Inc" },
    { 0xFCE6,   "Guard RFID Solutions Inc." },
    { 0xFCE7,   "TKH Security B.V." },
    { 0xFCE8,   "ITT Industries" },
    { 0xFCE9,   "MindRhythm, Inc." },
    { 0xFCEA,   "Chess Wise B.V." },
    { 0xFCEB,   "Avi-On" },
    { 0xFCEC,   "Griffwerk GmbH" },
    { 0xFCED,   "Workaround Gmbh" },
    { 0xFCEE,   "Velentium, LLC" },
    { 0xFCEF,   "Divesoft s.r.o." },
    { 0xFCF0,   "Security Enhancement Systems, LLC" },
    { 0xFCF1,   "Google LLC" },
    { 0xFCF2,   "Bitwards Oy" },
    { 0xFCF3,   "Armatura LLC" },
    { 0xFCF4,   "Allegion" },
    { 0xFCF5,   "Trident Communication Technology, LLC" },
    { 0xFCF6,   "The Linux Foundation" },
    { 0xFCF7,   "Honor Device Co., Ltd." },
    { 0xFCF8,   "Honor Device Co., Ltd." },
    { 0xFCF9,   "Leupold & Stevens, Inc." },
    { 0xFCFA,   "Leupold & Stevens, Inc." },
    { 0xFCFB,   "Shenzhen Benwei Media Co., Ltd." },
    { 0xFCFC,   "Barrot Technology Co.,Ltd." },
    { 0xFCFD,   "Barrot Technology Co.,Ltd." },
    { 0xFCFE,   "Sonova Consumer Hearing GmbH" },
    { 0xFCFF,   "701x" },
    { 0xFD00,   "FUTEK Advanced Sensor Technology, Inc." },
    { 0xFD01,   "Sanvita Medical Corporation" },
    { 0xFD02,   "LEGO System A/S" },
    { 0xFD03,   "Quuppa Oy" },
    { 0xFD04,   "Shure Inc." },
    { 0xFD05,   "Qualcomm Technologies, Inc." },
    { 0xFD06,   "RACE-AI LLC" },
    { 0xFD07,   "Swedlock AB" },
    { 0xFD08,   "Bull Group Incorporated Company" },
    { 0xFD09,   "Cousins and Sears LLC" },
    { 0xFD0A,   "Luminostics, Inc." },
    { 0xFD0B,   "Luminostics, Inc." },
    { 0xFD0C,   "OSM HK Limited" },
    { 0xFD0D,   "Blecon Ltd" },
    { 0xFD0E,   "HerdDogg, Inc" },
    { 0xFD0F,   "AEON MOTOR CO.,LTD." },
    { 0xFD10,   "AEON MOTOR CO.,LTD." },
    { 0xFD11,   "AEON MOTOR CO.,LTD." },
    { 0xFD12,   "AEON MOTOR CO.,LTD." },
    { 0xFD13,   "BRG Sports, Inc." },
    { 0xFD14,   "BRG Sports, Inc." },
    { 0xFD15,   "Panasonic Corporation" },
    { 0xFD16,   "Sensitech, Inc." },
    { 0xFD17,   "LEGIC Identsystems AG" },
    { 0xFD18,   "LEGIC Identsystems AG" },
    { 0xFD19,   "Smith & Nephew Medical Limited" },
    { 0xFD1A,   "CSIRO" },
    { 0xFD1B,   "Helios Sports, Inc." },
    { 0xFD1C,   "Brady Worldwide Inc." },
    { 0xFD1D,   "Samsung Electronics Co., Ltd" },
    { 0xFD1E,   "Plume Design Inc." },
    { 0xFD1F,   "3M" },
    { 0xFD20,   "GN Hearing A/S" },
    { 0xFD21,   "Huawei Technologies Co., Ltd." },
    { 0xFD22,   "Huawei Technologies Co., Ltd." },
    { 0xFD23,   "DOM Sicherheitstechnik GmbH & Co. KG" },
    { 0xFD24,   "GD Midea Air-Conditioning Equipment Co., Ltd." },
    { 0xFD25,   "GD Midea Air-Conditioning Equipment Co., Ltd." },
    { 0xFD26,   "Novo Nordisk A/S" },
    { 0xFD27,   "Integrated Illumination Systems, Inc." },
    { 0xFD28,   "Julius Blum GmbH" },
    { 0xFD29,   "Asahi Kasei Corporation" },
    { 0xFD2A,   "Sony Corporation" },
    { 0xFD2B,   "The Access Technologies" },
    { 0xFD2C,   "The Access Technologies" },
    { 0xFD2D,   "Xiaomi Inc." },
    { 0xFD2E,   "Bitstrata Systems Inc." },
    { 0xFD2F,   "Bitstrata Systems Inc." },
    { 0xFD30,   "Sesam Solutions BV" },
    { 0xFD31,   "LG Electronics Inc." },
    { 0xFD32,   "Gemalto Holding BV" },
    { 0xFD33,   "DashLogic, Inc." },
    { 0xFD34,   "Aerosens LLC." },
    { 0xFD35,   "Transsion Holdings Limited" },
    { 0xFD36,   "Google LLC" },
    { 0xFD37,   "TireCheck GmbH" },
    { 0xFD38,   "Danfoss A/S" },
    { 0xFD39,   "PREDIKTAS" },
    { 0xFD3A,   "Verkada Inc." },
    { 0xFD3B,   "Verkada Inc." },
    { 0xFD3C,   "Redline Communications Inc." },
    { 0xFD3D,   "Woan Technology (Shenzhen) Co., Ltd." },
    { 0xFD3E,   "Pure Watercraft, inc." },
    { 0xFD3F,   "Cognosos, Inc" },
    { 0xFD40,   "Beflex Inc." },
    { 0xFD41,   "Amazon Lab126" },
    { 0xFD42,   "Globe (Jiangsu) Co.,Ltd" },
    { 0xFD43,   "Apple Inc." },
    { 0xFD44,   "Apple Inc." },
    { 0xFD45,   "GB Solution co.,Ltd" },
    { 0xFD46,   "Lemco IKE" },
    { 0xFD47,   "Liberty Global Inc." },
    { 0xFD48,   "Geberit International AG" },
    { 0xFD49,   "Panasonic Corporation" },
    { 0xFD4A,   "Sigma Elektro GmbH" },
    { 0xFD4B,   "Samsung Electronics Co., Ltd." },
    { 0xFD4C,   "Adolf Wuerth GmbH & Co KG" },
    { 0xFD4D,   "70mai Co.,Ltd." },
    { 0xFD4E,   "70mai Co.,Ltd." },
    { 0xFD4F,   "SONITOR TECHNOLOGIES AS" },
    { 0xFD50,   "Hangzhou Tuya Information  Technology Co., Ltd" },
    { 0xFD51,   "UTC Fire and Security" },
    { 0xFD52,   "UTC Fire and Security" },
    { 0xFD53,   "PCI Private Limited" },
    { 0xFD54,   "Qingdao Haier Technology Co., Ltd." },
    { 0xFD55,   "Braveheart Wireless, Inc." },
    { 0xFD56,   "Resmed Ltd" },
    { 0xFD57,   "Volvo Car Corporation" },
    { 0xFD58,   "Volvo Car Corporation" },
    { 0xFD59,   "Samsung Electronics Co., Ltd." },
    { 0xFD5A,   "Samsung Electronics Co., Ltd." },
    { 0xFD5B,   "V2SOFT INC." },
    { 0xFD5C,   "React Mobile" },
    { 0xFD5D,   "maxon motor ltd." },
    { 0xFD5E,   "Tapkey GmbH" },
    { 0xFD5F,   "Meta Platforms Technologies, LLC" },
    { 0xFD60,   "Sercomm Corporation" },
    { 0xFD61,   "Arendi AG" },
    { 0xFD62,   "Google LLC" },
    { 0xFD63,   "Google LLC" },
    { 0xFD64,   "INRIA" },
    { 0xFD65,   "Razer Inc." },
    { 0xFD66,   "Zebra Technologies Corporation" },
    { 0xFD67,   "Montblanc Simplo GmbH" },
    { 0xFD68,   "Ubique Innovation AG" },
    { 0xFD69,   "Samsung Electronics Co., Ltd" },
    { 0xFD6A,   "Emerson" },
    { 0xFD6B,   "rapitag GmbH" },
    { 0xFD6C,   "Samsung Electronics Co., Ltd." },
    { 0xFD6D,   "Sigma Elektro GmbH" },
    { 0xFD6E,   "Polidea sp. z o.o." },
    { 0xFD6F,   "Apple, Inc." },
    { 0xFD70,   "GuangDong Oppo Mobile Telecommunications Corp., Ltd" },
    { 0xFD71,   "GN Hearing A/S" },
    { 0xFD72,   "Logitech International SA" },
    { 0xFD73,   "BRControls Products BV" },
    { 0xFD74,   "BRControls Products BV" },
    { 0xFD75,   "Insulet Corporation" },
    { 0xFD76,   "Insulet Corporation" },
    { 0xFD77,   "Withings" },
    { 0xFD78,   "Withings" },
    { 0xFD79,   "Withings" },
    { 0xFD7A,   "Withings" },
    { 0xFD7B,   "WYZE LABS, INC." },
    { 0xFD7C,   "Toshiba Information Systems(Japan) Corporation" },
    { 0xFD7D,   "Center for Advanced Research Wernher Von Braun" },
    { 0xFD7E,   "Samsung Electronics Co., Ltd." },
    { 0xFD7F,   "Husqvarna AB" },
    { 0xFD80,   "Phindex Technologies, Inc" },
    { 0xFD81,   "CANDY HOUSE, Inc." },
    { 0xFD82,   "Sony Corporation" },
    { 0xFD83,   "iNFORM Technology GmbH" },
    { 0xFD84,   "Tile, Inc." },
    { 0xFD85,   "Husqvarna AB" },
    { 0xFD86,   "Abbott" },
    { 0xFD87,   "Google LLC" },
    { 0xFD88,   "Urbanminded LTD" },
    { 0xFD89,   "Urbanminded LTD" },
    { 0xFD8A,   "Signify Netherlands B.V." },
    { 0xFD8B,   "Jigowatts Inc." },
    { 0xFD8C,   "Google LLC" },
    { 0xFD8D,   "quip NYC Inc." },
    { 0xFD8E,   "Motorola Solutions" },
    { 0xFD8F,   "Matrix ComSec Pvt. Ltd." },
    { 0xFD90,   "Guangzhou SuperSound Information Technology Co.,Ltd" },
    { 0xFD91,   "Groove X, Inc." },
    { 0xFD92,   "Qualcomm Technologies International, Ltd. (QTIL)" },
    { 0xFD93,   "Bayerische Motoren Werke AG" },
    { 0xFD94,   "Hewlett Packard Enterprise" },
    { 0xFD95,   "Rigado" },
    { 0xFD96,   "Google LLC" },
    { 0xFD97,   "June Life, Inc." },
    { 0xFD98,   "Disney Worldwide Services, Inc." },
    { 0xFD99,   "ABB Oy" },
    { 0xFD9A,   "Huawei Technologies Co., Ltd." },
    { 0xFD9B,   "Huawei Technologies Co., Ltd." },
    { 0xFD9C,   "Huawei Technologies Co., Ltd." },
    { 0xFD9D,   "Gastec Corporation" },
    { 0xFD9E,   "The Coca-Cola Company" },
    { 0xFD9F,   "VitalTech Affiliates LLC" },
    { 0xFDA0,   "Secugen Corporation" },
    { 0xFDA1,   "Groove X, Inc" },
    { 0xFDA2,   "Groove X, Inc" },
    { 0xFDA3,   "Inseego Corp." },
    { 0xFDA4,   "Inseego Corp." },
    { 0xFDA5,   "Neurostim OAB, Inc." },
    { 0xFDA6,   "WWZN Information Technology Company Limited" },
    { 0xFDA7,   "WWZN Information Technology Company Limited" },
    { 0xFDA8,   "PSA Peugeot Citroën" },
    { 0xFDA9,   "Rhombus Systems, Inc." },
    { 0xFDAA,   "Xiaomi Inc." },
    { 0xFDAB,   "Xiaomi Inc." },
    { 0xFDAC,   "Tentacle Sync GmbH" },
    { 0xFDAD,   "Houwa System Design, k.k." },
    { 0xFDAE,   "Houwa System Design, k.k." },
    { 0xFDAF,   "Wiliot LTD" },
    { 0xFDB0,   "Oura Health Ltd" },
    { 0xFDB1,   "Oura Health Ltd" },
    { 0xFDB2,   "Portable Multimedia Ltd" },
    { 0xFDB3,   "Audiodo AB" },
    { 0xFDB4,   "HP Inc" },
    { 0xFDB5,   "ECSG" },
    { 0xFDB6,   "GWA Hygiene GmbH" },
    { 0xFDB7,   "LivaNova USA Inc." },
    { 0xFDB8,   "LivaNova USA Inc." },
    { 0xFDB9,   "Comcast Cable Corporation" },
    { 0xFDBA,   "Comcast Cable Corporation" },
    { 0xFDBB,   "Profoto" },
    { 0xFDBC,   "Emerson" },
    { 0xFDBD,   "Clover Network, Inc." },
    { 0xFDBE,   "California Things Inc." },
    { 0xFDBF,   "California Things Inc." },
    { 0xFDC0,   "Hunter Douglas" },
    { 0xFDC1,   "Hunter Douglas" },
    { 0xFDC2,   "Baidu Online Network Technology (Beijing) Co., Ltd" },
    { 0xFDC3,   "Baidu Online Network Technology (Beijing) Co., Ltd" },
    { 0xFDC4,   "Simavita (Aust) Pty Ltd" },
    { 0xFDC5,   "Automatic Labs" },
    { 0xFDC6,   "Eli Lilly and Company" },
    { 0xFDC7,   "Eli Lilly and Company" },
    { 0xFDC8,   "Hach – Danaher" },
    { 0xFDC9,   "Busch-Jaeger Elektro GmbH" },
    { 0xFDCA,   "Fortin Electronic Systems" },
    { 0xFDCB,   "Meggitt SA" },
    { 0xFDCC,   "Shoof Technologies" },
    { 0xFDCD,   "Qingping Technology (Beijing) Co., Ltd." },
    { 0xFDCE,   "SENNHEISER electronic GmbH & Co. KG" },
    { 0xFDCF,   "Nalu Medical, Inc" },
    { 0xFDD0,   "Huawei Technologies Co., Ltd" },
    { 0xFDD1,   "Huawei Technologies Co., Ltd" },
    { 0xFDD2,   "Bose Corporation" },
    { 0xFDD3,   "FUBA Automotive Electronics GmbH" },
    { 0xFDD4,   "LX Solutions Pty Limited" },
    { 0xFDD5,   "Brompton Bicycle Ltd" },
    { 0xFDD6,   "Ministry of Supply" },
    { 0xFDD7,   "Emerson" },
    { 0xFDD8,   "Jiangsu Teranovo Tech Co., Ltd." },
    { 0xFDD9,   "Jiangsu Teranovo Tech Co., Ltd." },
    { 0xFDDA,   "MHCS" },
    { 0xFDDB,   "Samsung Electronics Co., Ltd." },
    { 0xFDDC,   "4iiii Innovations Inc." },
    { 0xFDDD,   "Arch Systems Inc" },
    { 0xFDDE,   "Noodle Technology Inc." },
    { 0xFDDF,   "Harman International" },
    { 0xFDE0,   "John Deere" },
    { 0xFDE1,   "Fortin Electronic Systems" },
    { 0xFDE2,   "Google LLC" },
    { 0xFDE3,   "Abbott Diabetes Care" },
    { 0xFDE4,   "JUUL Labs, Inc." },
    { 0xFDE5,   "SMK Corporation" },
    { 0xFDE6,   "Intelletto Technologies Inc" },
    { 0xFDE7,   "SECOM Co., LTD" },
    { 0xFDE8,   "Robert Bosch GmbH" },
    { 0xFDE9,   "Spacesaver Corporation" },
    { 0xFDEA,   "SeeScan, Inc" },
    { 0xFDEB,   "Syntronix Corporation" },
    { 0xFDEC,   "Mannkind Corporation" },
    { 0xFDED,   "Pole Star" },
    { 0xFDEE,   "Huawei Technologies Co., Ltd." },
    { 0xFDEF,   "ART AND PROGRAM, INC." },
    { 0xFDF0,   "Google LLC" },
    { 0xFDF1,   "LAMPLIGHT Co.,Ltd" },
    { 0xFDF2,   "AMICCOM Electronics Corporation" },
    { 0xFDF3,   "Amersports" },
    { 0xFDF4,   "O. E. M. Controls, Inc." },
    { 0xFDF5,   "Milwaukee Electric Tools" },
    { 0xFDF6,   "AIAIAI ApS" },
    { 0xFDF7,   "HP Inc." },
    { 0xFDF8,   "Onvocal" },
    { 0xFDF9,   "INIA" },
    { 0xFDFA,   "Tandem Diabetes Care" },
    { 0xFDFB,   "Tandem Diabetes Care" },
    { 0xFDFC,   "Optrel AG" },
    { 0xFDFD,   "RecursiveSoft Inc." },
    { 0xFDFE,   "ADHERIUM(NZ) LIMITED" },
    { 0xFDFF,   "OSRAM GmbH" },
    { 0xFE00,   "Amazon.com Services, Inc." },
    { 0xFE01,   "Duracell U.S. Operations Inc." },
    { 0xFE02,   "Robert Bosch GmbH" },
    { 0xFE03,   "Amazon.com Services, Inc." },
    { 0xFE04,   "Motorola Solutions, Inc." },
    { 0xFE05,   "CORE Transport Technologies NZ Limited" },
    { 0xFE06,   "Qualcomm Technologies, Inc." },
    { 0xFE07,   "Sonos, Inc." },
    { 0xFE08,   "Microsoft" },
    { 0xFE09,   "Pillsy, Inc." },
    { 0xFE0A,   "ruwido austria gmbh" },
    { 0xFE0B,   "ruwido austria gmbh" },
    { 0xFE0C,   "Procter & Gamble" },
    { 0xFE0D,   "Procter & Gamble" },
    { 0xFE0E,   "Setec Pty Ltd" },
    { 0xFE0F,   "Signify Netherlands B.V. (formerly Philips Lighting B.V.)" },
    { 0xFE10,   "LAPIS Technology Co., Ltd." },
    { 0xFE11,   "GMC-I Messtechnik GmbH" },
    { 0xFE12,   "M-Way Solutions GmbH" },
    { 0xFE13,   "Apple Inc." },
    { 0xFE14,   "Flextronics International USA Inc." },
    { 0xFE15,   "Amazon.com Services, Inc.." },
    { 0xFE16,   "Footmarks, Inc." },
    { 0xFE17,   "Telit Wireless Solutions GmbH" },
    { 0xFE18,   "Runtime, Inc." },
    { 0xFE19,   "Google LLC" },
    { 0xFE1A,   "Tyto Life LLC" },
    { 0xFE1B,   "Tyto Life LLC" },
    { 0xFE1C,   "NetMedia, Inc." },
    { 0xFE1D,   "Illuminati Instrument Corporation" },
    { 0xFE1E,   "LAMPLIGHT Co., Ltd." },
    { 0xFE1F,   "Garmin International, Inc." },
    { 0xFE20,   "Emerson" },
    { 0xFE21,   "Bose Corporation" },
    { 0xFE22,   "Zoll Medical Corporation" },
    { 0xFE23,   "Zoll Medical Corporation" },
    { 0xFE24,   "August Home Inc" },
    { 0xFE25,   "Apple, Inc." },
    { 0xFE26,   "Google LLC" },
    { 0xFE27,   "Google LLC" },
    { 0xFE28,   "Ayla Networks" },
    { 0xFE29,   "Gibson Innovations" },
    { 0xFE2A,   "DaisyWorks, Inc." },
    { 0xFE2B,   "ITT Industries" },
    { 0xFE2C,   "Google LLC" },
    { 0xFE2D,   "LAMPLIGHT Co., Ltd." },
    { 0xFE2E,   "ERi,Inc." },
    { 0xFE2F,   "CRESCO Wireless, Inc" },
    { 0xFE30,   "Volkswagen AG" },
    { 0xFE31,   "Volkswagen AG" },
    { 0xFE32,   "Pro-Mark, Inc." },
    { 0xFE33,   "CHIPOLO d.o.o." },
    { 0xFE34,   "SmallLoop LLC" },
    { 0xFE35,   "HUAWEI Technologies Co., Ltd" },
    { 0xFE36,   "HUAWEI Technologies Co., Ltd" },
    { 0xFE37,   "Spaceek LTD" },
    { 0xFE38,   "Spaceek LTD" },
    { 0xFE39,   "TTS Tooltechnic Systems AG & Co. KG" },
    { 0xFE3A,   "TTS Tooltechnic Systems AG & Co. KG" },
    { 0xFE3B,   "Dolby Laboratories" },
    { 0xFE3C,   "alibaba" },
    { 0xFE3D,   "BD Medical" },
    { 0xFE3E,   "BD Medical" },
    { 0xFE3F,   "Friday Labs Limited" },
    { 0xFE40,   "Inugo Systems Limited" },
    { 0xFE41,   "Inugo Systems Limited" },
    { 0xFE42,   "Nets A/S" },
    { 0xFE43,   "Andreas Stihl AG & Co. KG" },
    { 0xFE44,   "SK Telecom" },
    { 0xFE45,   "Snapchat Inc" },
    { 0xFE46,   "B&O Play A/S" },
    { 0xFE47,   "General Motors" },
    { 0xFE48,   "General Motors" },
    { 0xFE49,   "SenionLab AB" },
    { 0xFE4A,   "OMRON HEALTHCARE Co., Ltd." },
    { 0xFE4B,   "Signify Netherlands B.V. (formerly Philips Lighting B.V.)" },
    { 0xFE4C,   "Volkswagen AG" },
    { 0xFE4D,   "Casambi Technologies Oy" },
    { 0xFE4E,   "NTT docomo" },
    { 0xFE4F,   "Molekule, Inc." },
    { 0xFE50,   "Google LLC" },
    { 0xFE51,   "SRAM" },
    { 0xFE52,   "SetPoint Medical" },
    { 0xFE53,   "3M" },
    { 0xFE54,   "Motiv, Inc." },
    { 0xFE55,   "Google LLC" },
    { 0xFE56,   "Google LLC" },
    { 0xFE57,   "Dotted Labs" },
    { 0xFE58,   "Nordic Semiconductor ASA" },
    { 0xFE59,   "Nordic Semiconductor ASA" },
    { 0xFE5A,   "Cronologics Corporation" },
    { 0xFE5B,   "GT-tronics HK Ltd" },
    { 0xFE5C,   "million hunters GmbH" },
    { 0xFE5D,   "Grundfos A/S" },
    { 0xFE5E,   "Plastc Corporation" },
    { 0xFE5F,   "Eyefi, Inc." },
    { 0xFE60,   "Lierda Science & Technology Group Co., Ltd." },
    { 0xFE61,   "Logitech International SA" },
    { 0xFE62,   "Indagem Tech LLC" },
    { 0xFE63,   "Connected Yard, Inc." },
    { 0xFE64,   "Siemens AG" },
    { 0xFE65,   "CHIPOLO d.o.o." },
    { 0xFE66,   "Intel Corporation" },
    { 0xFE67,   "Lab Sensor Solutions" },
    { 0xFE68,   "Capsle Technologies Inc." },
    { 0xFE69,   "Capsle Technologies Inc." },
    { 0xFE6A,   "Kontakt Micro-Location Sp. z o.o." },
    { 0xFE6B,   "TASER International, Inc." },
    { 0xFE6C,   "TASER International, Inc." },
    { 0xFE6D,   "The University of Tokyo" },
    { 0xFE6E,   "The University of Tokyo" },
    { 0xFE6F,   "LINE Corporation" },
    { 0xFE70,   "Beijing Jingdong Century Trading Co., Ltd." },
    { 0xFE71,   "Plume Design Inc" },
    { 0xFE72,   "Abbott (formerly St. Jude Medical, Inc.)" },
    { 0xFE73,   "Abbott (formerly St. Jude Medical, Inc.)" },
    { 0xFE74,   "unwire" },
    { 0xFE75,   "TangoMe" },
    { 0xFE76,   "TangoMe" },
    { 0xFE77,   "Hewlett-Packard Company" },
    { 0xFE78,   "Hewlett-Packard Company" },
    { 0xFE79,   "Zebra Technologies" },
    { 0xFE7A,   "Bragi GmbH" },
    { 0xFE7B,   "Orion Labs, Inc." },
    { 0xFE7C,   "Telit Wireless Solutions (Formerly Stollmann E+V GmbH)" },
    { 0xFE7D,   "Aterica Health Inc." },
    { 0xFE7E,   "Awear Solutions Ltd" },
    { 0xFE7F,   "Doppler Lab" },
    { 0xFE80,   "Doppler Lab" },
    { 0xFE81,   "Medtronic Inc." },
    { 0xFE82,   "Medtronic Inc." },
    { 0xFE83,   "Blue Bite" },
    { 0xFE84,   "RF Digital Corp" },
    { 0xFE85,   "RF Digital Corp" },
    { 0xFE86,   "HUAWEI Technologies Co., Ltd" },
    { 0xFE87,   "Qingdao Yeelink Information Technology Co., Ltd. ( 青岛亿联客信息技术有限公司 )" },
    { 0xFE88,   "SALTO SYSTEMS S.L." },
    { 0xFE89,   "B&O Play A/S" },
    { 0xFE8A,   "Apple, Inc." },
    { 0xFE8B,   "Apple, Inc." },
    { 0xFE8C,   "TRON Forum" },
    { 0xFE8D,   "Interaxon Inc." },
    { 0xFE8E,   "ARM Ltd" },
    { 0xFE8F,   "CSR" },
    { 0xFE90,   "JUMA" },
    { 0xFE91,   "Shanghai Imilab Technology Co.,Ltd" },
    { 0xFE92,   "Jarden Safety & Security" },
    { 0xFE93,   "OttoQ In" },
    { 0xFE94,   "OttoQ In" },
    { 0xFE95,   "Xiaomi Inc." },
    { 0xFE96,   "Tesla Motors Inc." },
    { 0xFE97,   "Tesla Motors Inc." },
    { 0xFE98,   "Currant Inc" },
    { 0xFE99,   "Currant Inc" },
    { 0xFE9A,   "Estimote" },
    { 0xFE9B,   "Samsara Networks, Inc" },
    { 0xFE9C,   "GSI Laboratories, Inc." },
    { 0xFE9D,   "Mobiquity Networks Inc" },
    { 0xFE9E,   "Dialog Semiconductor B.V." },
    { 0xFE9F,   "Google LLC" },
    { 0xFEA0,   "Google LLC" },
    { 0xFEA1,   "Intrepid Control Systems, Inc." },
    { 0xFEA2,   "Intrepid Control Systems, Inc." },
    { 0xFEA3,   "ITT Industries" },
    { 0xFEA4,   "Paxton Access Ltd" },
    { 0xFEA5,   "GoPro, Inc." },
    { 0xFEA6,   "GoPro, Inc." },
    { 0xFEA7,   "UTC Fire and Security" },
    { 0xFEA8,   "Savant Systems LLC" },
    { 0xFEA9,   "Savant Systems LLC" },
    { 0xFEAA,   "Google LLC" },
    { 0xFEAB,   "Nokia" },
    { 0xFEAC,   "Nokia" },
    { 0xFEAD,   "Nokia" },
    { 0xFEAE,   "Nokia" },
    { 0xFEAF,   "Nest Labs Inc" },
    { 0xFEB0,   "Nest Labs Inc" },
    { 0xFEB1,   "Electronics Tomorrow Limited" },
    { 0xFEB2,   "Microsoft Corporation" },
    { 0xFEB3,   "Taobao" },
    { 0xFEB4,   "WiSilica Inc." },
    { 0xFEB5,   "WiSilica Inc." },
    { 0xFEB6,   "Vencer Co., Ltd" },
    { 0xFEB7,   "Meta Platforms, Inc." },
    { 0xFEB8,   "Meta Platforms, Inc." },
    { 0xFEB9,   "LG Electronics" },
    { 0xFEBA,   "Tencent Holdings Limited" },
    { 0xFEBB,   "adafruit industries" },
    { 0xFEBC,   "Dexcom Inc" },
    { 0xFEBD,   "Clover Network, Inc" },
    { 0xFEBE,   "Bose Corporation" },
    { 0xFEBF,   "Nod, Inc." },
    { 0xFEC0,   "KDDI Corporation" },
    { 0xFEC1,   "KDDI Corporation" },
    { 0xFEC2,   "Blue Spark Technologies, Inc." },
    { 0xFEC3,   "360fly, Inc." },
    { 0xFEC4,   "PLUS Location Systems" },
    { 0xFEC5,   "Realtek Semiconductor Corp." },
    { 0xFEC6,   "Kocomojo, LLC" },
    { 0xFEC7,   "Apple, Inc." },
    { 0xFEC8,   "Apple, Inc." },
    { 0xFEC9,   "Apple, Inc." },
    { 0xFECA,   "Apple, Inc." },
    { 0xFECB,   "Apple, Inc." },
    { 0xFECC,   "Apple, Inc." },
    { 0xFECD,   "Apple, Inc." },
    { 0xFECE,   "Apple, Inc." },
    { 0xFECF,   "Apple, Inc." },
    { 0xFED0,   "Apple, Inc." },
    { 0xFED1,   "Apple, Inc." },
    { 0xFED2,   "Apple, Inc." },
    { 0xFED3,   "Apple, Inc." },
    { 0xFED4,   "Apple, Inc." },
    { 0xFED5,   "Plantronics Inc." },
    { 0xFED6,   "Broadcom" },
    { 0xFED7,   "Broadcom" },
    { 0xFED8,   "Google LLC" },
    { 0xFED9,   "Pebble Technology Corporation" },
    { 0xFEDA,   "ISSC Technologies Corp." },
    { 0xFEDB,   "Perka, Inc." },
    { 0xFEDC,   "Jawbone" },
    { 0xFEDD,   "Jawbone" },
    { 0xFEDE,   "Coin, Inc." },
    { 0xFEDF,   "Design SHIFT" },
    { 0xFEE0,   "Anhui Huami Information Technology Co., Ltd." },
    { 0xFEE1,   "Anhui Huami Information Technology Co., Ltd." },
    { 0xFEE2,   "Anki, Inc." },
    { 0xFEE3,   "Anki, Inc." },
    { 0xFEE4,   "Nordic Semiconductor ASA" },
    { 0xFEE5,   "Nordic Semiconductor ASA" },
    { 0xFEE6,   "Silvair, Inc." },
    { 0xFEE7,   "Tencent Holdings Limited." },
    { 0xFEE8,   "Quintic Corp." },
    { 0xFEE9,   "Quintic Corp." },
    { 0xFEEA,   "Swirl Networks, Inc." },
    { 0xFEEB,   "Swirl Networks, Inc." },
    { 0xFEEC,   "Tile, Inc." },
    { 0xFEED,   "Tile, Inc." },
    { 0xFEEE,   "Polar Electro Oy" },
    { 0xFEEF,   "Polar Electro Oy" },
    { 0xFEF0,   "Intel" },
    { 0xFEF1,   "CSR" },
    { 0xFEF2,   "CSR" },
    { 0xFEF3,   "Google LLC" },
    { 0xFEF4,   "Google LLC" },
    { 0xFEF5,   "Dialog Semiconductor GmbH" },
    { 0xFEF6,   "Wicentric, Inc." },
    { 0xFEF7,   "Aplix Corporation" },
    { 0xFEF8,   "Aplix Corporation" },
    { 0xFEF9,   "PayPal, Inc." },
    { 0xFEFA,   "PayPal, Inc." },
    { 0xFEFB,   "Telit Wireless Solutions (Formerly Stollmann E+V GmbH)" },
    { 0xFEFC,   "Gimbal, Inc." },
    { 0xFEFD,   "Gimbal, Inc." },
    { 0xFEFE,   "GN Hearing A/S" },
    { 0xFEFF,   "GN Netcom" },
    /* SDO - https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/uuids/sdo_uuids.yaml */
    { 0xFFEF,   "Wi-Fi Direct Specification" },
    { 0xFFF0,   "Public Key Open Credential (PKOC)" },
    { 0xFFF1,   "ICCE Digital Key" },
    { 0xFFF2,   "Aliro" },
    { 0xFFF3,   "FiRa Consortium" },
    { 0xFFF4,   "FiRa Consortium" },
    { 0xFFF5,   "Car Connectivity Consortium, LLC" },
    { 0xFFF6,   "Matter Profile ID" },
    { 0xFFF7,   "Zigbee Direct" },
    { 0xFFF8,   "Mopria Alliance BLE" },
    { 0xFFF9,   "FIDO2 secure client-to-authenticator transport" },
    { 0xFFFA,   "ASTM Remote ID" },
    { 0xFFFB,   "Direct Thread Commissioning" },
    { 0xFFFC,   "Wireless Power Transfer (WPT)" },
    { 0xFFFD,   "Universal Second Factor Authenticator" },
    { 0xFFFE,   "Wireless Power Transfer" },
    {      0,   NULL }
};
value_string_ext bluetooth_uuid_vals_ext = VALUE_STRING_EXT_INIT(bluetooth_uuid_vals);

/* Taken from https://bitbucket.org/bluetooth-SIG/public/raw/HEAD/assigned_numbers/company_identifiers/company_identifiers.yaml */
static const value_string bluetooth_company_id_vals[] = {
    { 0x0000,   "Ericsson AB" },
    { 0x0001,   "Nokia Mobile Phones" },
    { 0x0002,   "Intel Corp." },
    { 0x0003,   "IBM Corp." },
    { 0x0004,   "Toshiba Corp." },
    { 0x0005,   "3Com" },
    { 0x0006,   "Microsoft" },
    { 0x0007,   "Lucent" },
    { 0x0008,   "Motorola" },
    { 0x0009,   "Infineon Technologies AG" },
    { 0x000A,   "Qualcomm Technologies International, Ltd. (QTIL)" },
    { 0x000B,   "Silicon Wave" },
    { 0x000C,   "Digianswer A/S" },
    { 0x000D,   "Texas Instruments Inc." },
    { 0x000E,   "Parthus Technologies Inc." },
    { 0x000F,   "Broadcom Corporation" },
    { 0x0010,   "Mitel Semiconductor" },
    { 0x0011,   "Widcomm, Inc." },
    { 0x0012,   "Zeevo, Inc." },
    { 0x0013,   "Atmel Corporation" },
    { 0x0014,   "Mitsubishi Electric Corporation" },
    { 0x0015,   "RTX A/S" },
    { 0x0016,   "KC Technology Inc." },
    { 0x0017,   "Newlogic" },
    { 0x0018,   "Transilica, Inc." },
    { 0x0019,   "Rohde & Schwarz GmbH & Co. KG" },
    { 0x001A,   "TTPCom Limited" },
    { 0x001B,   "Signia Technologies, Inc." },
    { 0x001C,   "Conexant Systems Inc." },
    { 0x001D,   "Qualcomm" },
    { 0x001E,   "Inventel" },
    { 0x001F,   "AVM Berlin" },
    { 0x0020,   "BandSpeed, Inc." },
    { 0x0021,   "Mansella Ltd" },
    { 0x0022,   "NEC Corporation" },
    { 0x0023,   "WavePlus Technology Co., Ltd." },
    { 0x0024,   "Alcatel" },
    { 0x0025,   "NXP B.V." },
    { 0x0026,   "C Technologies" },
    { 0x0027,   "Open Interface" },
    { 0x0028,   "R F Micro Devices" },
    { 0x0029,   "Hitachi Ltd" },
    { 0x002A,   "Symbol Technologies, Inc." },
    { 0x002B,   "Tenovis" },
    { 0x002C,   "Macronix International Co. Ltd." },
    { 0x002D,   "GCT Semiconductor" },
    { 0x002E,   "Norwood Systems" },
    { 0x002F,   "MewTel Technology Inc." },
    { 0x0030,   "ST Microelectronics" },
    { 0x0031,   "Synopsys, Inc." },
    { 0x0032,   "Red-M (Communications) Ltd" },
    { 0x0033,   "Commil Ltd" },
    { 0x0034,   "Computer Access Technology Corporation (CATC)" },
    { 0x0035,   "Eclipse (HQ Espana) S.L." },
    { 0x0036,   "Renesas Electronics Corporation" },
    { 0x0037,   "Mobilian Corporation" },
    { 0x0038,   "Syntronix Corporation" },
    { 0x0039,   "Integrated System Solution Corp." },
    { 0x003A,   "Panasonic Holdings Corporation" },
    { 0x003B,   "Gennum Corporation" },
    { 0x003C,   "BlackBerry Limited" },
    { 0x003D,   "IPextreme, Inc." },
    { 0x003E,   "Systems and Chips, Inc" },
    { 0x003F,   "Bluetooth SIG, Inc" },
    { 0x0040,   "Seiko Epson Corporation" },
    { 0x0041,   "Integrated Silicon Solution Taiwan, Inc." },
    { 0x0042,   "CONWISE Technology Corporation Ltd" },
    { 0x0043,   "PARROT AUTOMOTIVE SAS" },
    { 0x0044,   "Socket Mobile" },
    { 0x0045,   "Atheros Communications, Inc." },
    { 0x0046,   "MediaTek, Inc." },
    { 0x0047,   "Bluegiga" },
    { 0x0048,   "Marvell Technology Group Ltd." },
    { 0x0049,   "3DSP Corporation" },
    { 0x004A,   "Accel Semiconductor Ltd." },
    { 0x004B,   "Continental Automotive Systems" },
    { 0x004C,   "Apple, Inc." },
    { 0x004D,   "Staccato Communications, Inc." },
    { 0x004E,   "Avago Technologies" },
    { 0x004F,   "APT Ltd." },
    { 0x0050,   "SiRF Technology, Inc." },
    { 0x0051,   "Tzero Technologies, Inc." },
    { 0x0052,   "J&M Corporation" },
    { 0x0053,   "Free2move AB" },
    { 0x0054,   "3DiJoy Corporation" },
    { 0x0055,   "Plantronics, Inc." },
    { 0x0056,   "Sony Ericsson Mobile Communications" },
    { 0x0057,   "Harman International Industries, Inc." },
    { 0x0058,   "Vizio, Inc." },
    { 0x0059,   "Nordic Semiconductor ASA" },
    { 0x005A,   "EM Microelectronic-Marin SA" },
    { 0x005B,   "Ralink Technology Corporation" },
    { 0x005C,   "Belkin International, Inc." },
    { 0x005D,   "Realtek Semiconductor Corporation" },
    { 0x005E,   "Stonestreet One, LLC" },
    { 0x005F,   "Wicentric, Inc." },
    { 0x0060,   "RivieraWaves S.A.S" },
    { 0x0061,   "RDA Microelectronics" },
    { 0x0062,   "Gibson Guitars" },
    { 0x0063,   "MiCommand Inc." },
    { 0x0064,   "Band XI International, LLC" },
    { 0x0065,   "HP, Inc." },
    { 0x0066,   "9Solutions Oy" },
    { 0x0067,   "GN Audio A/S" },
    { 0x0068,   "General Motors" },
    { 0x0069,   "A&D Engineering, Inc." },
    { 0x006A,   "LTIMINDTREE LIMITED" },
    { 0x006B,   "Polar Electro OY" },
    { 0x006C,   "Beautiful Enterprise Co., Ltd." },
    { 0x006D,   "BriarTek, Inc" },
    { 0x006E,   "Summit Data Communications, Inc." },
    { 0x006F,   "Sound ID" },
    { 0x0070,   "Monster, LLC" },
    { 0x0071,   "connectBlue AB" },
    { 0x0072,   "ShangHai Super Smart Electronics Co. Ltd." },
    { 0x0073,   "Group Sense Ltd." },
    { 0x0074,   "Zomm, LLC" },
    { 0x0075,   "Samsung Electronics Co. Ltd." },
    { 0x0076,   "Creative Technology Ltd." },
    { 0x0077,   "Laird Connectivity LLC" },
    { 0x0078,   "Nike, Inc." },
    { 0x0079,   "lesswire AG" },
    { 0x007A,   "MStar Semiconductor, Inc." },
    { 0x007B,   "Hanlynn Technologies" },
    { 0x007C,   "A & R Cambridge" },
    { 0x007D,   "Seers Technology Co., Ltd." },
    { 0x007E,   "Sports Tracking Technologies Ltd." },
    { 0x007F,   "Autonet Mobile" },
    { 0x0080,   "DeLorme Publishing Company, Inc." },
    { 0x0081,   "WuXi Vimicro" },
    { 0x0082,   "DSEA A/S" },
    { 0x0083,   "TimeKeeping Systems, Inc." },
    { 0x0084,   "Ludus Helsinki Ltd." },
    { 0x0085,   "BlueRadios, Inc." },
    { 0x0086,   "Equinux AG" },
    { 0x0087,   "Garmin International, Inc." },
    { 0x0088,   "Ecotest" },
    { 0x0089,   "GN Hearing A/S" },
    { 0x008A,   "Jawbone" },
    { 0x008B,   "Topcon Positioning Systems, LLC" },
    { 0x008C,   "Gimbal Inc." },
    { 0x008D,   "Zscan Software" },
    { 0x008E,   "Quintic Corp" },
    { 0x008F,   "Telit Wireless Solutions GmbH" },
    { 0x0090,   "Funai Electric Co., Ltd." },
    { 0x0091,   "Advanced PANMOBIL systems GmbH & Co. KG" },
    { 0x0092,   "ThinkOptics, Inc." },
    { 0x0093,   "Universal Electronics, Inc." },
    { 0x0094,   "Airoha Technology Corp." },
    { 0x0095,   "NEC Lighting, Ltd." },
    { 0x0096,   "ODM Technology, Inc." },
    { 0x0097,   "ConnecteDevice Ltd." },
    { 0x0098,   "zero1.tv GmbH" },
    { 0x0099,   "i.Tech Dynamic Global Distribution Ltd." },
    { 0x009A,   "Alpwise" },
    { 0x009B,   "Jiangsu Toppower Automotive Electronics Co., Ltd." },
    { 0x009C,   "Colorfy, Inc." },
    { 0x009D,   "Geoforce Inc." },
    { 0x009E,   "Bose Corporation" },
    { 0x009F,   "Suunto Oy" },
    { 0x00A0,   "Kensington Computer Products Group" },
    { 0x00A1,   "SR-Medizinelektronik" },
    { 0x00A2,   "Vertu Corporation Limited" },
    { 0x00A3,   "Meta Watch Ltd." },
    { 0x00A4,   "LINAK A/S" },
    { 0x00A5,   "OTL Dynamics LLC" },
    { 0x00A6,   "Panda Ocean Inc." },
    { 0x00A7,   "Visteon Corporation" },
    { 0x00A8,   "ARP Devices Limited" },
    { 0x00A9,   "MARELLI EUROPE S.P.A." },
    { 0x00AA,   "CAEN RFID srl" },
    { 0x00AB,   "Ingenieur-Systemgruppe Zahn GmbH" },
    { 0x00AC,   "Green Throttle Games" },
    { 0x00AD,   "Peter Systemtechnik GmbH" },
    { 0x00AE,   "Omegawave Oy" },
    { 0x00AF,   "Cinetix" },
    { 0x00B0,   "Passif Semiconductor Corp" },
    { 0x00B1,   "Saris Cycling Group, Inc" },
    { 0x00B2,   "Bekey A/S" },
    { 0x00B3,   "Clarinox Technologies Pty. Ltd." },
    { 0x00B4,   "BDE Technology Co., Ltd." },
    { 0x00B5,   "Swirl Networks" },
    { 0x00B6,   "Meso international" },
    { 0x00B7,   "TreLab Ltd" },
    { 0x00B8,   "Qualcomm Innovation Center, Inc. (QuIC)" },
    { 0x00B9,   "Johnson Controls, Inc." },
    { 0x00BA,   "Starkey Hearing Technologies" },
    { 0x00BB,   "S-Power Electronics Limited" },
    { 0x00BC,   "Ace Sensor Inc" },
    { 0x00BD,   "Aplix Corporation" },
    { 0x00BE,   "AAMP of America" },
    { 0x00BF,   "Stalmart Technology Limited" },
    { 0x00C0,   "AMICCOM Electronics Corporation" },
    { 0x00C1,   "Shenzhen Excelsecu Data Technology Co.,Ltd" },
    { 0x00C2,   "Geneq Inc." },
    { 0x00C3,   "adidas AG" },
    { 0x00C4,   "LG Electronics" },
    { 0x00C5,   "Onset Computer Corporation" },
    { 0x00C6,   "Selfly BV" },
    { 0x00C7,   "Quuppa Oy." },
    { 0x00C8,   "GeLo Inc" },
    { 0x00C9,   "Evluma" },
    { 0x00CA,   "MC10" },
    { 0x00CB,   "Binauric SE" },
    { 0x00CC,   "Beats Electronics" },
    { 0x00CD,   "Microchip Technology Inc." },
    { 0x00CE,   "Eve Systems GmbH" },
    { 0x00CF,   "ARCHOS SA" },
    { 0x00D0,   "Dexcom, Inc." },
    { 0x00D1,   "Polar Electro Europe B.V." },
    { 0x00D2,   "Dialog Semiconductor B.V." },
    { 0x00D3,   "Taixingbang Technology (HK) Co,. LTD." },
    { 0x00D4,   "Kawantech" },
    { 0x00D5,   "Austco Communication Systems" },
    { 0x00D6,   "Timex Group USA, Inc." },
    { 0x00D7,   "Qualcomm Technologies, Inc." },
    { 0x00D8,   "Qualcomm Connected Experiences, Inc." },
    { 0x00D9,   "Voyetra Turtle Beach" },
    { 0x00DA,   "txtr GmbH" },
    { 0x00DB,   "Snuza (Pty) Ltd" },
    { 0x00DC,   "Procter & Gamble" },
    { 0x00DD,   "Hosiden Corporation" },
    { 0x00DE,   "Muzik LLC" },
    { 0x00DF,   "Misfit Wearables Corp" },
    { 0x00E0,   "Google" },
    { 0x00E1,   "Danlers Ltd" },
    { 0x00E2,   "Semilink Inc" },
    { 0x00E3,   "inMusic Brands, Inc" },
    { 0x00E4,   "L.S. Research, Inc." },
    { 0x00E5,   "Eden Software Consultants Ltd." },
    { 0x00E6,   "Freshtemp" },
    { 0x00E7,   "KS Technologies" },
    { 0x00E8,   "ACTS Technologies" },
    { 0x00E9,   "Vtrack Systems" },
    { 0x00EA,   "www.vtracksystems.com" },
    { 0x00EB,   "Server Technology Inc." },
    { 0x00EC,   "BioResearch Associates" },
    { 0x00ED,   "Jolly Logic, LLC" },
    { 0x00EE,   "Above Average Outcomes, Inc." },
    { 0x00EF,   "Bitsplitters GmbH" },
    { 0x00F0,   "PayPal, Inc." },
    { 0x00F1,   "Witron Technology Limited" },
    { 0x00F2,   "Morse Project Inc." },
    { 0x00F3,   "Kent Displays Inc." },
    { 0x00F4,   "Nautilus Inc." },
    { 0x00F5,   "Smartifier Oy" },
    { 0x00F6,   "Elcometer Limited" },
    { 0x00F7,   "VSN Technologies, Inc." },
    { 0x00F8,   "AceUni Corp., Ltd." },
    { 0x00F9,   "StickNFind" },
    { 0x00FA,   "Crystal Alarm AB" },
    { 0x00FB,   "KOUKAAM a.s." },
    { 0x00FC,   "Delphi Corporation" },
    { 0x00FD,   "ValenceTech Limited" },
    { 0x00FE,   "Stanley Black and Decker" },
    { 0x00FF,   "Typo Products, LLC" },
    { 0x0100,   "TomTom International BV" },
    { 0x0101,   "Fugoo, Inc." },
    { 0x0102,   "Keiser Corporation" },
    { 0x0103,   "Bang & Olufsen A/S" },
    { 0x0104,   "PLUS Location Systems Pty Ltd" },
    { 0x0105,   "Ubiquitous Computing Technology Corporation" },
    { 0x0106,   "Innovative Yachtter Solutions" },
    { 0x0107,   "Demant A/S" },
    { 0x0108,   "Chicony Electronics Co., Ltd." },
    { 0x0109,   "Atus BV" },
    { 0x010A,   "Codegate Ltd" },
    { 0x010B,   "ERi, Inc" },
    { 0x010C,   "Transducers Direct, LLC" },
    { 0x010D,   "DENSO TEN Limited" },
    { 0x010E,   "Audi AG" },
    { 0x010F,   "HiSilicon Technologies CO., LIMITED" },
    { 0x0110,   "Nippon Seiki Co., Ltd." },
    { 0x0111,   "Steelseries ApS" },
    { 0x0112,   "Visybl Inc." },
    { 0x0113,   "Openbrain Technologies, Co., Ltd." },
    { 0x0114,   "Xensr" },
    { 0x0115,   "e.solutions" },
    { 0x0116,   "10AK Technologies" },
    { 0x0117,   "Wimoto Technologies Inc" },
    { 0x0118,   "Radius Networks, Inc." },
    { 0x0119,   "Wize Technology Co., Ltd." },
    { 0x011A,   "Qualcomm Labs, Inc." },
    { 0x011B,   "Hewlett Packard Enterprise" },
    { 0x011C,   "Baidu" },
    { 0x011D,   "Arendi AG" },
    { 0x011E,   "Skoda Auto a.s." },
    { 0x011F,   "Volkswagen AG" },
    { 0x0120,   "Porsche AG" },
    { 0x0121,   "Sino Wealth Electronic Ltd." },
    { 0x0122,   "AirTurn, Inc." },
    { 0x0123,   "Kinsa, Inc" },
    { 0x0124,   "HID Global" },
    { 0x0125,   "SEAT es" },
    { 0x0126,   "Promethean Ltd." },
    { 0x0127,   "Salutica Allied Solutions" },
    { 0x0128,   "GPSI Group Pty Ltd" },
    { 0x0129,   "Nimble Devices Oy" },
    { 0x012A,   "Changzhou Yongse Infotech  Co., Ltd." },
    { 0x012B,   "SportIQ" },
    { 0x012C,   "TEMEC Instruments B.V." },
    { 0x012D,   "Sony Corporation" },
    { 0x012E,   "ASSA ABLOY" },
    { 0x012F,   "Clarion Co. Inc." },
    { 0x0130,   "Warehouse Innovations" },
    { 0x0131,   "Cypress Semiconductor" },
    { 0x0132,   "MADS Inc" },
    { 0x0133,   "Blue Maestro Limited" },
    { 0x0134,   "Resolution Products, Ltd." },
    { 0x0135,   "Aireware LLC" },
    { 0x0136,   "Silvair, Inc." },
    { 0x0137,   "Prestigio Plaza Ltd." },
    { 0x0138,   "NTEO Inc." },
    { 0x0139,   "Focus Systems Corporation" },
    { 0x013A,   "Tencent Holdings Ltd." },
    { 0x013B,   "Allegion" },
    { 0x013C,   "Murata Manufacturing Co., Ltd." },
    { 0x013D,   "WirelessWERX" },
    { 0x013E,   "Nod, Inc." },
    { 0x013F,   "B&B Manufacturing Company" },
    { 0x0140,   "Alpine Electronics (China) Co., Ltd" },
    { 0x0141,   "FedEx Services" },
    { 0x0142,   "Grape Systems Inc." },
    { 0x0143,   "Bkon Connect" },
    { 0x0144,   "Lintech GmbH" },
    { 0x0145,   "Novatel Wireless" },
    { 0x0146,   "Ciright" },
    { 0x0147,   "Mighty Cast, Inc." },
    { 0x0148,   "Ambimat Electronics" },
    { 0x0149,   "Perytons Ltd." },
    { 0x014A,   "Tivoli Audio, LLC" },
    { 0x014B,   "Master Lock" },
    { 0x014C,   "Mesh-Net Ltd" },
    { 0x014D,   "HUIZHOU DESAY SV AUTOMOTIVE CO., LTD." },
    { 0x014E,   "Tangerine, Inc." },
    { 0x014F,   "B&W Group Ltd." },
    { 0x0150,   "Pioneer Corporation" },
    { 0x0151,   "OnBeep" },
    { 0x0152,   "Vernier Software & Technology" },
    { 0x0153,   "ROL Ergo" },
    { 0x0154,   "Pebble Technology" },
    { 0x0155,   "NETATMO" },
    { 0x0156,   "Accumulate AB" },
    { 0x0157,   "Anhui Huami Information Technology Co., Ltd." },
    { 0x0158,   "Inmite s.r.o." },
    { 0x0159,   "ChefSteps, Inc." },
    { 0x015A,   "micas AG" },
    { 0x015B,   "Biomedical Research Ltd." },
    { 0x015C,   "Pitius Tec S.L." },
    { 0x015D,   "Estimote, Inc." },
    { 0x015E,   "Unikey Technologies, Inc." },
    { 0x015F,   "Timer Cap Co." },
    { 0x0160,   "AwoX" },
    { 0x0161,   "yikes" },
    { 0x0162,   "MADSGlobalNZ Ltd." },
    { 0x0163,   "PCH International" },
    { 0x0164,   "Qingdao Yeelink Information Technology Co., Ltd." },
    { 0x0165,   "Milwaukee Electric Tools" },
    { 0x0166,   "MISHIK Pte Ltd" },
    { 0x0167,   "Ascensia Diabetes Care US Inc." },
    { 0x0168,   "Spicebox LLC" },
    { 0x0169,   "emberlight" },
    { 0x016A,   "Copeland Cold Chain LP" },
    { 0x016B,   "Qblinks" },
    { 0x016C,   "MYSPHERA" },
    { 0x016D,   "LifeScan Inc" },
    { 0x016E,   "Volantic AB" },
    { 0x016F,   "Podo Labs, Inc" },
    { 0x0170,   "Roche Diabetes Care AG" },
    { 0x0171,   "Amazon.com Services LLC" },
    { 0x0172,   "Connovate Technology Private Limited" },
    { 0x0173,   "Kocomojo, LLC" },
    { 0x0174,   "Everykey Inc." },
    { 0x0175,   "Dynamic Controls" },
    { 0x0176,   "SentriLock" },
    { 0x0177,   "I-SYST inc." },
    { 0x0178,   "CASIO COMPUTER CO., LTD." },
    { 0x0179,   "LAPIS Semiconductor Co.,Ltd" },
    { 0x017A,   "Telemonitor, Inc." },
    { 0x017B,   "taskit GmbH" },
    { 0x017C,   "Mercedes-Benz Group AG" },
    { 0x017D,   "BatAndCat" },
    { 0x017E,   "BluDotz Ltd" },
    { 0x017F,   "XTel Wireless ApS" },
    { 0x0180,   "Gigaset Technologies GmbH" },
    { 0x0181,   "Gecko Health Innovations, Inc." },
    { 0x0182,   "HOP Ubiquitous" },
    { 0x0183,   "Walt Disney" },
    { 0x0184,   "Nectar" },
    { 0x0185,   "bel'apps LLC" },
    { 0x0186,   "CORE Lighting Ltd" },
    { 0x0187,   "Seraphim Sense Ltd" },
    { 0x0188,   "Unico RBC" },
    { 0x0189,   "Physical Enterprises Inc." },
    { 0x018A,   "Able Trend Technology Limited" },
    { 0x018B,   "Konica Minolta, Inc." },
    { 0x018C,   "Wilo SE" },
    { 0x018D,   "Extron Design Services" },
    { 0x018E,   "Google LLC" },
    { 0x018F,   "Fireflies Systems" },
    { 0x0190,   "Intelletto Technologies Inc." },
    { 0x0191,   "FDK CORPORATION" },
    { 0x0192,   "Cloudleaf, Inc" },
    { 0x0193,   "Maveric Automation LLC" },
    { 0x0194,   "Acoustic Stream Corporation" },
    { 0x0195,   "Zuli" },
    { 0x0196,   "Paxton Access Ltd" },
    { 0x0197,   "WiSilica Inc." },
    { 0x0198,   "VENGIT Korlatolt Felelossegu Tarsasag" },
    { 0x0199,   "SALTO SYSTEMS S.L." },
    { 0x019A,   "TRON Forum" },
    { 0x019B,   "CUBETECH s.r.o." },
    { 0x019C,   "Cokiya Incorporated" },
    { 0x019D,   "CVS Health" },
    { 0x019E,   "Ceruus" },
    { 0x019F,   "Strainstall Ltd" },
    { 0x01A0,   "Channel Enterprises (HK) Ltd." },
    { 0x01A1,   "FIAMM" },
    { 0x01A2,   "GIGALANE.CO.,LTD" },
    { 0x01A3,   "EROAD" },
    { 0x01A4,   "MSA Innovation, LLC" },
    { 0x01A5,   "Icon Health and Fitness" },
    { 0x01A6,   "Wille Engineering" },
    { 0x01A7,   "ENERGOUS CORPORATION" },
    { 0x01A8,   "Taobao" },
    { 0x01A9,   "Canon Inc." },
    { 0x01AA,   "Geophysical Technology Inc." },
    { 0x01AB,   "Meta Platforms, Inc." },
    { 0x01AC,   "Trividia Health, Inc." },
    { 0x01AD,   "FlightSafety International" },
    { 0x01AE,   "Earlens Corporation" },
    { 0x01AF,   "Sunrise Micro Devices, Inc." },
    { 0x01B0,   "Star Micronics Co., Ltd." },
    { 0x01B1,   "Netizens Sp. z o.o." },
    { 0x01B2,   "Nymi Inc." },
    { 0x01B3,   "Nytec, Inc." },
    { 0x01B4,   "Trineo Sp. z o.o." },
    { 0x01B5,   "Nest Labs Inc." },
    { 0x01B6,   "LM Technologies Ltd" },
    { 0x01B7,   "General Electric Company" },
    { 0x01B8,   "i+D3 S.L." },
    { 0x01B9,   "HANA Micron" },
    { 0x01BA,   "Stages Cycling LLC" },
    { 0x01BB,   "Cochlear Bone Anchored Solutions AB" },
    { 0x01BC,   "SenionLab AB" },
    { 0x01BD,   "Syszone Co., Ltd" },
    { 0x01BE,   "Pulsate Mobile Ltd." },
    { 0x01BF,   "Hongkong OnMicro Electronics Limited" },
    { 0x01C0,   "pironex GmbH" },
    { 0x01C1,   "BRADATECH Corp." },
    { 0x01C2,   "Transenergooil AG" },
    { 0x01C3,   "Bunch" },
    { 0x01C4,   "DME Microelectronics" },
    { 0x01C5,   "Bitcraze AB" },
    { 0x01C6,   "HASWARE Inc." },
    { 0x01C7,   "Abiogenix Inc." },
    { 0x01C8,   "Poly-Control ApS" },
    { 0x01C9,   "Avi-on" },
    { 0x01CA,   "Laerdal Medical AS" },
    { 0x01CB,   "Fetch My Pet" },
    { 0x01CC,   "Sam Labs Ltd." },
    { 0x01CD,   "Chengdu Synwing Technology Ltd" },
    { 0x01CE,   "HOUWA SYSTEM DESIGN, k.k." },
    { 0x01CF,   "BSH" },
    { 0x01D0,   "Primus Inter Pares Ltd" },
    { 0x01D1,   "August Home, Inc" },
    { 0x01D2,   "Gill Electronics" },
    { 0x01D3,   "Sky Wave Design" },
    { 0x01D4,   "Newlab S.r.l." },
    { 0x01D5,   "ELAD srl" },
    { 0x01D6,   "G-wearables inc." },
    { 0x01D7,   "Squadrone Systems Inc." },
    { 0x01D8,   "Code Corporation" },
    { 0x01D9,   "Savant Systems LLC" },
    { 0x01DA,   "Logitech International SA" },
    { 0x01DB,   "Innblue Consulting" },
    { 0x01DC,   "iParking Ltd." },
    { 0x01DD,   "Koninklijke Philips N.V." },
    { 0x01DE,   "Minelab Electronics Pty Limited" },
    { 0x01DF,   "Bison Group Ltd." },
    { 0x01E0,   "Widex A/S" },
    { 0x01E1,   "Jolla Ltd" },
    { 0x01E2,   "Lectronix, Inc." },
    { 0x01E3,   "Caterpillar Inc" },
    { 0x01E4,   "Freedom Innovations" },
    { 0x01E5,   "Dynamic Devices Ltd" },
    { 0x01E6,   "Technology Solutions (UK) Ltd" },
    { 0x01E7,   "IPS Group Inc." },
    { 0x01E8,   "STIR" },
    { 0x01E9,   "Sano, Inc." },
    { 0x01EA,   "Advanced Application Design, Inc." },
    { 0x01EB,   "AutoMap LLC" },
    { 0x01EC,   "Spreadtrum Communications Shanghai Ltd" },
    { 0x01ED,   "CuteCircuit LTD" },
    { 0x01EE,   "Valeo Service" },
    { 0x01EF,   "Fullpower Technologies, Inc." },
    { 0x01F0,   "KloudNation" },
    { 0x01F1,   "Zebra Technologies Corporation" },
    { 0x01F2,   "Itron, Inc." },
    { 0x01F3,   "The University of Tokyo" },
    { 0x01F4,   "UTC Fire and Security" },
    { 0x01F5,   "Cool Webthings Limited" },
    { 0x01F6,   "DJO Global" },
    { 0x01F7,   "Gelliner Limited" },
    { 0x01F8,   "Anyka (Guangzhou) Microelectronics Technology Co, LTD" },
    { 0x01F9,   "Medtronic Inc." },
    { 0x01FA,   "Gozio Inc." },
    { 0x01FB,   "Form Lifting, LLC" },
    { 0x01FC,   "Wahoo Fitness, LLC" },
    { 0x01FD,   "Kontakt Micro-Location Sp. z o.o." },
    { 0x01FE,   "Radio Systems Corporation" },
    { 0x01FF,   "Freescale Semiconductor, Inc." },
    { 0x0200,   "Verifone Systems Pte Ltd. Taiwan Branch" },
    { 0x0201,   "AR Timing" },
    { 0x0202,   "Rigado LLC" },
    { 0x0203,   "Kemppi Oy" },
    { 0x0204,   "Tapcentive Inc." },
    { 0x0205,   "Smartbotics Inc." },
    { 0x0206,   "Otter Products, LLC" },
    { 0x0207,   "STEMP Inc." },
    { 0x0208,   "LumiGeek LLC" },
    { 0x0209,   "InvisionHeart Inc." },
    { 0x020A,   "Macnica Inc." },
    { 0x020B,   "Jaguar Land Rover Limited" },
    { 0x020C,   "CoroWare Technologies, Inc" },
    { 0x020D,   "Simplo Technology Co., LTD" },
    { 0x020E,   "Omron Healthcare Co., LTD" },
    { 0x020F,   "Comodule GMBH" },
    { 0x0210,   "ikeGPS" },
    { 0x0211,   "Telink Semiconductor Co. Ltd" },
    { 0x0212,   "Interplan Co., Ltd" },
    { 0x0213,   "Wyler AG" },
    { 0x0214,   "IK Multimedia Production srl" },
    { 0x0215,   "Lukoton Experience Oy" },
    { 0x0216,   "MTI Ltd" },
    { 0x0217,   "Tech4home, Lda" },
    { 0x0218,   "Hiotech AB" },
    { 0x0219,   "DOTT Limited" },
    { 0x021A,   "Blue Speck Labs, LLC" },
    { 0x021B,   "Cisco Systems, Inc" },
    { 0x021C,   "Mobicomm Inc" },
    { 0x021D,   "Edamic" },
    { 0x021E,   "Goodnet, Ltd" },
    { 0x021F,   "Luster Leaf Products  Inc" },
    { 0x0220,   "Manus Machina BV" },
    { 0x0221,   "Mobiquity Networks Inc" },
    { 0x0222,   "Praxis Dynamics" },
    { 0x0223,   "Philip Morris Products S.A." },
    { 0x0224,   "Comarch SA" },
    { 0x0225,   "Nestlé Nespresso S.A." },
    { 0x0226,   "Merlinia A/S" },
    { 0x0227,   "LifeBEAM Technologies" },
    { 0x0228,   "Twocanoes Labs, LLC" },
    { 0x0229,   "Muoverti Limited" },
    { 0x022A,   "Stamer Musikanlagen GMBH" },
    { 0x022B,   "Tesla, Inc." },
    { 0x022C,   "Pharynks Corporation" },
    { 0x022D,   "Lupine" },
    { 0x022E,   "Siemens AG" },
    { 0x022F,   "Huami (Shanghai) Culture Communication CO., LTD" },
    { 0x0230,   "Foster Electric Company, Ltd" },
    { 0x0231,   "ETA SA" },
    { 0x0232,   "x-Senso Solutions Kft" },
    { 0x0233,   "Shenzhen SuLong Communication Ltd" },
    { 0x0234,   "FengFan (BeiJing) Technology Co, Ltd" },
    { 0x0235,   "Qrio Inc" },
    { 0x0236,   "Pitpatpet Ltd" },
    { 0x0237,   "MSHeli s.r.l." },
    { 0x0238,   "Trakm8 Ltd" },
    { 0x0239,   "JIN CO, Ltd" },
    { 0x023A,   "Alatech Tehnology" },
    { 0x023B,   "Beijing CarePulse Electronic Technology Co, Ltd" },
    { 0x023C,   "Awarepoint" },
    { 0x023D,   "ViCentra B.V." },
    { 0x023E,   "Raven Industries" },
    { 0x023F,   "WaveWare Technologies Inc." },
    { 0x0240,   "Argenox Technologies" },
    { 0x0241,   "Bragi GmbH" },
    { 0x0242,   "16Lab Inc" },
    { 0x0243,   "Masimo Corp" },
    { 0x0244,   "Iotera Inc" },
    { 0x0245,   "Endress+Hauser" },
    { 0x0246,   "ACKme Networks, Inc." },
    { 0x0247,   "FiftyThree Inc." },
    { 0x0248,   "Parker Hannifin Corp" },
    { 0x0249,   "Transcranial Ltd" },
    { 0x024A,   "Uwatec AG" },
    { 0x024B,   "Orlan LLC" },
    { 0x024C,   "Blue Clover Devices" },
    { 0x024D,   "M-Way Solutions GmbH" },
    { 0x024E,   "Microtronics Engineering GmbH" },
    { 0x024F,   "Schneider Schreibgeräte GmbH" },
    { 0x0250,   "Sapphire Circuits LLC" },
    { 0x0251,   "Lumo Bodytech Inc." },
    { 0x0252,   "UKC Technosolution" },
    { 0x0253,   "Xicato Inc." },
    { 0x0254,   "Playbrush" },
    { 0x0255,   "Dai Nippon Printing Co., Ltd." },
    { 0x0256,   "G24 Power Limited" },
    { 0x0257,   "AdBabble Local Commerce Inc." },
    { 0x0258,   "Devialet SA" },
    { 0x0259,   "ALTYOR" },
    { 0x025A,   "University of Applied Sciences Valais/Haute Ecole Valaisanne" },
    { 0x025B,   "Five Interactive, LLC dba Zendo" },
    { 0x025C,   "NetEase（Hangzhou）Network co.Ltd." },
    { 0x025D,   "Lexmark International Inc." },
    { 0x025E,   "Fluke Corporation" },
    { 0x025F,   "Yardarm Technologies" },
    { 0x0260,   "SensaRx" },
    { 0x0261,   "SECVRE GmbH" },
    { 0x0262,   "Glacial Ridge Technologies" },
    { 0x0263,   "Identiv, Inc." },
    { 0x0264,   "DDS, Inc." },
    { 0x0265,   "SMK Corporation" },
    { 0x0266,   "Schawbel Technologies LLC" },
    { 0x0267,   "XMI Systems SA" },
    { 0x0268,   "Cerevo" },
    { 0x0269,   "Torrox GmbH & Co KG" },
    { 0x026A,   "Gemalto" },
    { 0x026B,   "DEKA Research & Development Corp." },
    { 0x026C,   "Domster Tadeusz Szydlowski" },
    { 0x026D,   "Technogym SPA" },
    { 0x026E,   "FLEURBAEY BVBA" },
    { 0x026F,   "Aptcode Solutions" },
    { 0x0270,   "LSI ADL Technology" },
    { 0x0271,   "Animas Corp" },
    { 0x0272,   "Alps Alpine Co., Ltd." },
    { 0x0273,   "OCEASOFT" },
    { 0x0274,   "Motsai Research" },
    { 0x0275,   "Geotab" },
    { 0x0276,   "E.G.O. Elektro-Geraetebau GmbH" },
    { 0x0277,   "bewhere inc" },
    { 0x0278,   "Johnson Outdoors Inc" },
    { 0x0279,   "steute Schaltgerate GmbH & Co. KG" },
    { 0x027A,   "Ekomini inc." },
    { 0x027B,   "DEFA AS" },
    { 0x027C,   "Aseptika Ltd" },
    { 0x027D,   "HUAWEI Technologies Co., Ltd." },
    { 0x027E,   "HabitAware, LLC" },
    { 0x027F,   "ruwido austria gmbh" },
    { 0x0280,   "ITEC corporation" },
    { 0x0281,   "StoneL" },
    { 0x0282,   "Sonova AG" },
    { 0x0283,   "Maven Machines, Inc." },
    { 0x0284,   "Synapse Electronics" },
    { 0x0285,   "WOWTech Canada Ltd." },
    { 0x0286,   "RF Code, Inc." },
    { 0x0287,   "Wally Ventures S.L." },
    { 0x0288,   "Willowbank Electronics Ltd" },
    { 0x0289,   "SK Telecom" },
    { 0x028A,   "Jetro AS" },
    { 0x028B,   "Code Gears LTD" },
    { 0x028C,   "NANOLINK APS" },
    { 0x028D,   "IF, LLC" },
    { 0x028E,   "RF Digital Corp" },
    { 0x028F,   "Church & Dwight Co., Inc" },
    { 0x0290,   "Multibit Oy" },
    { 0x0291,   "CliniCloud Inc" },
    { 0x0292,   "SwiftSensors" },
    { 0x0293,   "Blue Bite" },
    { 0x0294,   "ELIAS GmbH" },
    { 0x0295,   "Sivantos GmbH" },
    { 0x0296,   "Petzl" },
    { 0x0297,   "storm power ltd" },
    { 0x0298,   "EISST Ltd" },
    { 0x0299,   "Inexess Technology Simma KG" },
    { 0x029A,   "Currant, Inc." },
    { 0x029B,   "C2 Development, Inc." },
    { 0x029C,   "Blue Sky Scientific, LLC" },
    { 0x029D,   "ALOTTAZS LABS, LLC" },
    { 0x029E,   "Kupson spol. s r.o." },
    { 0x029F,   "Areus Engineering GmbH" },
    { 0x02A0,   "Impossible Camera GmbH" },
    { 0x02A1,   "InventureTrack Systems" },
    { 0x02A2,   "Sera4 Ltd." },
    { 0x02A3,   "Itude" },
    { 0x02A4,   "Pacific Lock Company" },
    { 0x02A5,   "Tendyron Corporation" },
    { 0x02A6,   "Robert Bosch GmbH" },
    { 0x02A7,   "Illuxtron international B.V." },
    { 0x02A8,   "miSport Ltd." },
    { 0x02A9,   "Chargelib" },
    { 0x02AA,   "Doppler Lab" },
    { 0x02AB,   "BBPOS Limited" },
    { 0x02AC,   "RTB Elektronik GmbH & Co. KG" },
    { 0x02AD,   "Rx Networks, Inc." },
    { 0x02AE,   "WeatherFlow, Inc." },
    { 0x02AF,   "Technicolor USA Inc." },
    { 0x02B0,   "Bestechnic(Shanghai),Ltd" },
    { 0x02B1,   "Raden Inc" },
    { 0x02B2,   "Oura Health Oy" },
    { 0x02B3,   "CLABER S.P.A." },
    { 0x02B4,   "Hyginex, Inc." },
    { 0x02B5,   "HANSHIN ELECTRIC RAILWAY CO.,LTD." },
    { 0x02B6,   "Schneider Electric" },
    { 0x02B7,   "Oort Technologies LLC" },
    { 0x02B8,   "Chrono Therapeutics" },
    { 0x02B9,   "Rinnai Corporation" },
    { 0x02BA,   "Swissprime Technologies AG" },
    { 0x02BB,   "Koha.,Co.Ltd" },
    { 0x02BC,   "Genevac Ltd" },
    { 0x02BD,   "Chemtronics" },
    { 0x02BE,   "Seguro Technology Sp. z o.o." },
    { 0x02BF,   "Redbird Flight Simulations" },
    { 0x02C0,   "Dash Robotics" },
    { 0x02C1,   "LINE Corporation" },
    { 0x02C2,   "Guillemot Corporation" },
    { 0x02C3,   "Techtronic Power Tools Technology Limited" },
    { 0x02C4,   "Wilson Sporting Goods" },
    { 0x02C5,   "Lenovo (Singapore) Pte Ltd." },
    { 0x02C6,   "Ayatan Sensors" },
    { 0x02C7,   "Electronics Tomorrow Limited" },
    { 0x02C8,   "OneSpan" },
    { 0x02C9,   "PayRange Inc." },
    { 0x02CA,   "ABOV Semiconductor" },
    { 0x02CB,   "AINA-Wireless Inc." },
    { 0x02CC,   "Eijkelkamp Soil & Water" },
    { 0x02CD,   "BMA ergonomics b.v." },
    { 0x02CE,   "Teva Branded Pharmaceutical Products R&D, Inc." },
    { 0x02CF,   "Anima" },
    { 0x02D0,   "3M" },
    { 0x02D1,   "Empatica Srl" },
    { 0x02D2,   "Afero, Inc." },
    { 0x02D3,   "Powercast Corporation" },
    { 0x02D4,   "Secuyou ApS" },
    { 0x02D5,   "OMRON Corporation" },
    { 0x02D6,   "Send Solutions" },
    { 0x02D7,   "NIPPON SYSTEMWARE CO.,LTD." },
    { 0x02D8,   "Neosfar" },
    { 0x02D9,   "Fliegl Agrartechnik GmbH" },
    { 0x02DA,   "Gilvader" },
    { 0x02DB,   "Digi International Inc (R)" },
    { 0x02DC,   "DeWalch Technologies, Inc." },
    { 0x02DD,   "Flint Rehabilitation Devices, LLC" },
    { 0x02DE,   "Samsung SDS Co., Ltd." },
    { 0x02DF,   "Blur Product Development" },
    { 0x02E0,   "University of Michigan" },
    { 0x02E1,   "Victron Energy BV" },
    { 0x02E2,   "NTT docomo" },
    { 0x02E3,   "Carmanah Technologies Corp." },
    { 0x02E4,   "Bytestorm Ltd." },
    { 0x02E5,   "Espressif Systems (Shanghai) Co., Ltd." },
    { 0x02E6,   "Unwire" },
    { 0x02E7,   "Connected Yard, Inc." },
    { 0x02E8,   "American Music Environments" },
    { 0x02E9,   "Sensogram Technologies, Inc." },
    { 0x02EA,   "Fujitsu Limited" },
    { 0x02EB,   "Ardic Technology" },
    { 0x02EC,   "Delta Systems, Inc" },
    { 0x02ED,   "HTC Corporation" },
    { 0x02EE,   "Citizen Holdings Co., Ltd." },
    { 0x02EF,   "SMART-INNOVATION.inc" },
    { 0x02F0,   "Blackrat Software" },
    { 0x02F1,   "The Idea Cave, LLC" },
    { 0x02F2,   "GoPro, Inc." },
    { 0x02F3,   "AuthAir, Inc" },
    { 0x02F4,   "Vensi, Inc." },
    { 0x02F5,   "Indagem Tech LLC" },
    { 0x02F6,   "Intemo Technologies" },
    { 0x02F7,   "DreamVisions co., Ltd." },
    { 0x02F8,   "Runteq Oy Ltd" },
    { 0x02F9,   "IMAGINATION TECHNOLOGIES LTD" },
    { 0x02FA,   "CoSTAR TEchnologies" },
    { 0x02FB,   "Clarius Mobile Health Corp." },
    { 0x02FC,   "Shanghai Frequen Microelectronics Co., Ltd." },
    { 0x02FD,   "Uwanna, Inc." },
    { 0x02FE,   "Lierda Science & Technology Group Co., Ltd." },
    { 0x02FF,   "Silicon Laboratories" },
    { 0x0300,   "World Moto Inc." },
    { 0x0301,   "Giatec Scientific Inc." },
    { 0x0302,   "Loop Devices, Inc" },
    { 0x0303,   "IACA electronique" },
    { 0x0304,   "Oura Health Ltd" },
    { 0x0305,   "Swipp ApS" },
    { 0x0306,   "Life Laboratory Inc." },
    { 0x0307,   "FUJI INDUSTRIAL CO.,LTD." },
    { 0x0308,   "Surefire, LLC" },
    { 0x0309,   "Dolby Labs" },
    { 0x030A,   "Ellisys" },
    { 0x030B,   "Magnitude Lighting Converters" },
    { 0x030C,   "Hilti AG" },
    { 0x030D,   "Devdata S.r.l." },
    { 0x030E,   "Deviceworx" },
    { 0x030F,   "Shortcut Labs" },
    { 0x0310,   "SGL Italia S.r.l." },
    { 0x0311,   "PEEQ DATA" },
    { 0x0312,   "Ducere Technologies Pvt Ltd" },
    { 0x0313,   "DiveNav, Inc." },
    { 0x0314,   "RIIG AI Sp. z o.o." },
    { 0x0315,   "Thermo Fisher Scientific" },
    { 0x0316,   "AG Measurematics Pvt. Ltd." },
    { 0x0317,   "CHUO Electronics CO., LTD." },
    { 0x0318,   "Aspenta International" },
    { 0x0319,   "Eugster Frismag AG" },
    { 0x031A,   "Wurth Elektronik eiSos GmbH & Co. KG" },
    { 0x031B,   "HQ Inc" },
    { 0x031C,   "Lab Sensor Solutions" },
    { 0x031D,   "Enterlab ApS" },
    { 0x031E,   "Eyefi, Inc." },
    { 0x031F,   "MetaSystem S.p.A." },
    { 0x0320,   "SONO ELECTRONICS. CO., LTD" },
    { 0x0321,   "Jewelbots" },
    { 0x0322,   "Compumedics Limited" },
    { 0x0323,   "Rotor Bike Components" },
    { 0x0324,   "Astro, Inc." },
    { 0x0325,   "Amotus Solutions" },
    { 0x0326,   "Healthwear Technologies (Changzhou)Ltd" },
    { 0x0327,   "Essex Electronics" },
    { 0x0328,   "Grundfos A/S" },
    { 0x0329,   "Eargo, Inc." },
    { 0x032A,   "Electronic Design Lab" },
    { 0x032B,   "ESYLUX" },
    { 0x032C,   "NIPPON SMT.CO.,Ltd" },
    { 0x032D,   "BM innovations GmbH" },
    { 0x032E,   "indoormap" },
    { 0x032F,   "OttoQ Inc" },
    { 0x0330,   "North Pole Engineering" },
    { 0x0331,   "3flares Technologies Inc." },
    { 0x0332,   "Electrocompaniet A.S." },
    { 0x0333,   "Mul-T-Lock" },
    { 0x0334,   "Airthings ASA" },
    { 0x0335,   "Enlighted Inc" },
    { 0x0336,   "GISTIC" },
    { 0x0337,   "AJP2 Holdings, LLC" },
    { 0x0338,   "COBI GmbH" },
    { 0x0339,   "Blue Sky Scientific, LLC" },
    { 0x033A,   "Appception, Inc." },
    { 0x033B,   "Courtney Thorne Limited" },
    { 0x033C,   "Virtuosys" },
    { 0x033D,   "TPV Technology Limited" },
    { 0x033E,   "Monitra SA" },
    { 0x033F,   "Automation Components, Inc." },
    { 0x0340,   "Letsense s.r.l." },
    { 0x0341,   "Etesian Technologies LLC" },
    { 0x0342,   "GERTEC BRASIL LTDA." },
    { 0x0343,   "Drekker Development Pty. Ltd." },
    { 0x0344,   "Whirl Inc" },
    { 0x0345,   "Locus Positioning" },
    { 0x0346,   "Acuity Brands Lighting, Inc" },
    { 0x0347,   "Prevent Biometrics" },
    { 0x0348,   "Arioneo" },
    { 0x0349,   "VersaMe" },
    { 0x034A,   "Vaddio" },
    { 0x034B,   "Libratone A/S" },
    { 0x034C,   "HM Electronics, Inc." },
    { 0x034D,   "TASER International, Inc." },
    { 0x034E,   "SafeTrust Inc." },
    { 0x034F,   "Heartland Payment Systems" },
    { 0x0350,   "Bitstrata Systems Inc." },
    { 0x0351,   "Pieps GmbH" },
    { 0x0352,   "iRiding(Xiamen)Technology Co.,Ltd." },
    { 0x0353,   "Alpha Audiotronics, Inc." },
    { 0x0354,   "TOPPAN FORMS CO.,LTD." },
    { 0x0355,   "Sigma Designs, Inc." },
    { 0x0356,   "Spectrum Brands, Inc." },
    { 0x0357,   "Polymap Wireless" },
    { 0x0358,   "MagniWare Ltd." },
    { 0x0359,   "Novotec Medical GmbH" },
    { 0x035A,   "Phillips-Medisize A/S" },
    { 0x035B,   "Matrix Inc." },
    { 0x035C,   "Eaton Corporation" },
    { 0x035D,   "KYS" },
    { 0x035E,   "Naya Health, Inc." },
    { 0x035F,   "Acromag" },
    { 0x0360,   "Insulet Corporation" },
    { 0x0361,   "Wellinks Inc." },
    { 0x0362,   "ON Semiconductor" },
    { 0x0363,   "FREELAP SA" },
    { 0x0364,   "Favero Electronics Srl" },
    { 0x0365,   "BioMech Sensor LLC" },
    { 0x0366,   "BOLTT Sports technologies Private limited" },
    { 0x0367,   "Saphe International" },
    { 0x0368,   "Metormote AB" },
    { 0x0369,   "littleBits" },
    { 0x036A,   "SetPoint Medical" },
    { 0x036B,   "BRControls Products BV" },
    { 0x036C,   "Zipcar" },
    { 0x036D,   "AirBolt Pty Ltd" },
    { 0x036E,   "MOTIVE TECHNOLOGIES, INC." },
    { 0x036F,   "Motiv, Inc." },
    { 0x0370,   "Wazombi Labs OÜ" },
    { 0x0371,   "ORBCOMM" },
    { 0x0372,   "Nixie Labs, Inc." },
    { 0x0373,   "AppNearMe Ltd" },
    { 0x0374,   "Holman Industries" },
    { 0x0375,   "Expain AS" },
    { 0x0376,   "Electronic Temperature Instruments Ltd" },
    { 0x0377,   "Plejd AB" },
    { 0x0378,   "Propeller Health" },
    { 0x0379,   "Shenzhen iMCO Electronic Technology Co.,Ltd" },
    { 0x037A,   "Algoria" },
    { 0x037B,   "Apption Labs Inc." },
    { 0x037C,   "Cronologics Corporation" },
    { 0x037D,   "MICRODIA Ltd." },
    { 0x037E,   "lulabytes S.L." },
    { 0x037F,   "Société des Produits Nestlé S.A." },
    { 0x0380,   "LLC \"MEGA-F service\"" },
    { 0x0381,   "Sharp Corporation" },
    { 0x0382,   "Precision Outcomes Ltd" },
    { 0x0383,   "Kronos Incorporated" },
    { 0x0384,   "OCOSMOS Co., Ltd." },
    { 0x0385,   "Embedded Electronic Solutions Ltd. dba e2Solutions" },
    { 0x0386,   "Aterica Inc." },
    { 0x0387,   "BluStor PMC, Inc." },
    { 0x0388,   "Kapsch TrafficCom AB" },
    { 0x0389,   "ActiveBlu Corporation" },
    { 0x038A,   "Kohler Mira Limited" },
    { 0x038B,   "Noke" },
    { 0x038C,   "Appion Inc." },
    { 0x038D,   "Resmed Ltd" },
    { 0x038E,   "Crownstone B.V." },
    { 0x038F,   "Xiaomi Inc." },
    { 0x0390,   "INFOTECH s.r.o." },
    { 0x0391,   "Thingsquare AB" },
    { 0x0392,   "T&D" },
    { 0x0393,   "LAVAZZA S.p.A." },
    { 0x0394,   "Netclearance Systems, Inc." },
    { 0x0395,   "SDATAWAY" },
    { 0x0396,   "BLOKS GmbH" },
    { 0x0397,   "LEGO System A/S" },
    { 0x0398,   "Thetatronics Ltd" },
    { 0x0399,   "Nikon Corporation" },
    { 0x039A,   "NeST" },
    { 0x039B,   "South Silicon Valley Microelectronics" },
    { 0x039C,   "ALE International" },
    { 0x039D,   "CareView Communications, Inc." },
    { 0x039E,   "SchoolBoard Limited" },
    { 0x039F,   "Molex Corporation" },
    { 0x03A0,   "IVT Wireless Limited" },
    { 0x03A1,   "Alpine Labs LLC" },
    { 0x03A2,   "Candura Instruments" },
    { 0x03A3,   "SmartMovt Technology Co., Ltd" },
    { 0x03A4,   "Token Zero Ltd" },
    { 0x03A5,   "ACE CAD Enterprise Co., Ltd. (ACECAD)" },
    { 0x03A6,   "Medela, Inc" },
    { 0x03A7,   "AeroScout" },
    { 0x03A8,   "Esrille Inc." },
    { 0x03A9,   "THINKERLY SRL" },
    { 0x03AA,   "Exon Sp. z o.o." },
    { 0x03AB,   "Meizu Technology Co., Ltd." },
    { 0x03AC,   "Smablo LTD" },
    { 0x03AD,   "XiQ" },
    { 0x03AE,   "Allswell Inc." },
    { 0x03AF,   "Comm-N-Sense Corp DBA Verigo" },
    { 0x03B0,   "VIBRADORM GmbH" },
    { 0x03B1,   "Otodata Wireless Network Inc." },
    { 0x03B2,   "Propagation Systems Limited" },
    { 0x03B3,   "Midwest Instruments & Controls" },
    { 0x03B4,   "Alpha Nodus, inc." },
    { 0x03B5,   "petPOMM, Inc" },
    { 0x03B6,   "Mattel" },
    { 0x03B7,   "Airbly Inc." },
    { 0x03B8,   "A-Safe Limited" },
    { 0x03B9,   "FREDERIQUE CONSTANT SA" },
    { 0x03BA,   "Maxscend Microelectronics Company Limited" },
    { 0x03BB,   "Abbott" },
    { 0x03BC,   "ASB Bank Ltd" },
    { 0x03BD,   "amadas" },
    { 0x03BE,   "Applied Science, Inc." },
    { 0x03BF,   "iLumi Solutions Inc." },
    { 0x03C0,   "Arch Systems Inc." },
    { 0x03C1,   "Ember Technologies, Inc." },
    { 0x03C2,   "Snapchat Inc" },
    { 0x03C3,   "Casambi Technologies Oy" },
    { 0x03C4,   "Pico Technology Inc." },
    { 0x03C5,   "St. Jude Medical, Inc." },
    { 0x03C6,   "Intricon" },
    { 0x03C7,   "Structural Health Systems, Inc." },
    { 0x03C8,   "Avvel International" },
    { 0x03C9,   "Gallagher Group" },
    { 0x03CA,   "In2things Automation Pvt. Ltd." },
    { 0x03CB,   "SYSDEV Srl" },
    { 0x03CC,   "Vonkil Technologies Ltd" },
    { 0x03CD,   "Wynd Technologies, Inc." },
    { 0x03CE,   "CONTRINEX S.A." },
    { 0x03CF,   "MIRA, Inc." },
    { 0x03D0,   "Watteam Ltd" },
    { 0x03D1,   "Density Inc." },
    { 0x03D2,   "IOT Pot India Private Limited" },
    { 0x03D3,   "Sigma Connectivity AB" },
    { 0x03D4,   "PEG PEREGO SPA" },
    { 0x03D5,   "Wyzelink Systems Inc." },
    { 0x03D6,   "Yota Devices LTD" },
    { 0x03D7,   "FINSECUR" },
    { 0x03D8,   "Zen-Me Labs Ltd" },
    { 0x03D9,   "3IWare Co., Ltd." },
    { 0x03DA,   "EnOcean GmbH" },
    { 0x03DB,   "Instabeat, Inc" },
    { 0x03DC,   "Nima Labs" },
    { 0x03DD,   "Andreas Stihl AG & Co. KG" },
    { 0x03DE,   "Nathan Rhoades LLC" },
    { 0x03DF,   "Grob Technologies, LLC" },
    { 0x03E0,   "Actions (Zhuhai) Technology Co., Limited" },
    { 0x03E1,   "SPD Development Company Ltd" },
    { 0x03E2,   "Sensoan Oy" },
    { 0x03E3,   "Qualcomm Life Inc" },
    { 0x03E4,   "Chip-ing AG" },
    { 0x03E5,   "ffly4u" },
    { 0x03E6,   "IoT Instruments Oy" },
    { 0x03E7,   "TRUE Fitness Technology" },
    { 0x03E8,   "Reiner Kartengeraete GmbH & Co. KG." },
    { 0x03E9,   "SHENZHEN LEMONJOY TECHNOLOGY CO., LTD." },
    { 0x03EA,   "Hello Inc." },
    { 0x03EB,   "Ozo Edu, Inc." },
    { 0x03EC,   "Jigowatts Inc." },
    { 0x03ED,   "BASIC MICRO.COM,INC." },
    { 0x03EE,   "CUBE TECHNOLOGIES" },
    { 0x03EF,   "foolography GmbH" },
    { 0x03F0,   "CLINK" },
    { 0x03F1,   "Hestan Smart Cooking Inc." },
    { 0x03F2,   "WindowMaster A/S" },
    { 0x03F3,   "Flowscape AB" },
    { 0x03F4,   "PAL Technologies Ltd" },
    { 0x03F5,   "WHERE, Inc." },
    { 0x03F6,   "Iton Technology Corp." },
    { 0x03F7,   "Owl Labs Inc." },
    { 0x03F8,   "Rockford Corp." },
    { 0x03F9,   "Becon Technologies Co.,Ltd." },
    { 0x03FA,   "Vyassoft Technologies Inc" },
    { 0x03FB,   "Nox Medical" },
    { 0x03FC,   "Kimberly-Clark" },
    { 0x03FD,   "Trimble Inc." },
    { 0x03FE,   "Littelfuse" },
    { 0x03FF,   "Withings" },
    { 0x0400,   "i-developer IT Beratung UG" },
    { 0x0401,   "Relations Inc." },
    { 0x0402,   "Sears Holdings Corporation" },
    { 0x0403,   "Gantner Electronic GmbH" },
    { 0x0404,   "Authomate Inc" },
    { 0x0405,   "Vertex International, Inc." },
    { 0x0406,   "Airtago" },
    { 0x0407,   "Swiss Audio SA" },
    { 0x0408,   "ToGetHome Inc." },
    { 0x0409,   "RYSE INC." },
    { 0x040A,   "ZF OPENMATICS s.r.o." },
    { 0x040B,   "Jana Care Inc." },
    { 0x040C,   "Senix Corporation" },
    { 0x040D,   "NorthStar Battery Company, LLC" },
    { 0x040E,   "SKF (U.K.) Limited" },
    { 0x040F,   "CO-AX Technology, Inc." },
    { 0x0410,   "Fender Musical Instruments" },
    { 0x0411,   "Luidia Inc" },
    { 0x0412,   "SEFAM" },
    { 0x0413,   "Wireless Cables Inc" },
    { 0x0414,   "Lightning Protection International Pty Ltd" },
    { 0x0415,   "Uber Technologies Inc" },
    { 0x0416,   "SODA GmbH" },
    { 0x0417,   "Fatigue Science" },
    { 0x0418,   "Alpine Electronics Inc." },
    { 0x0419,   "Novalogy LTD" },
    { 0x041A,   "Friday Labs Limited" },
    { 0x041B,   "OrthoAccel Technologies" },
    { 0x041C,   "WaterGuru, Inc." },
    { 0x041D,   "Benning Elektrotechnik und Elektronik GmbH & Co. KG" },
    { 0x041E,   "Dell Computer Corporation" },
    { 0x041F,   "Kopin Corporation" },
    { 0x0420,   "TecBakery GmbH" },
    { 0x0421,   "Backbone Labs, Inc." },
    { 0x0422,   "DELSEY SA" },
    { 0x0423,   "Chargifi Limited" },
    { 0x0424,   "Trainesense Ltd." },
    { 0x0425,   "Unify Software and Solutions GmbH & Co. KG" },
    { 0x0426,   "Husqvarna AB" },
    { 0x0427,   "Focus fleet and fuel management inc" },
    { 0x0428,   "SmallLoop, LLC" },
    { 0x0429,   "Prolon Inc." },
    { 0x042A,   "BD Medical" },
    { 0x042B,   "iMicroMed Incorporated" },
    { 0x042C,   "Ticto N.V." },
    { 0x042D,   "Meshtech AS" },
    { 0x042E,   "MemCachier Inc." },
    { 0x042F,   "Danfoss A/S" },
    { 0x0430,   "SnapStyk Inc." },
    { 0x0431,   "Alticor Inc." },
    { 0x0432,   "Silk Labs, Inc." },
    { 0x0433,   "Pillsy Inc." },
    { 0x0434,   "Hatch Baby, Inc." },
    { 0x0435,   "Blocks Wearables Ltd." },
    { 0x0436,   "Drayson Technologies (Europe) Limited" },
    { 0x0437,   "eBest IOT Inc." },
    { 0x0438,   "Helvar Ltd" },
    { 0x0439,   "Radiance Technologies" },
    { 0x043A,   "Nuheara Limited" },
    { 0x043B,   "Appside co., ltd." },
    { 0x043C,   "DeLaval" },
    { 0x043D,   "Coiler Corporation" },
    { 0x043E,   "Thermomedics, Inc." },
    { 0x043F,   "Tentacle Sync GmbH" },
    { 0x0440,   "Valencell, Inc." },
    { 0x0441,   "iProtoXi Oy" },
    { 0x0442,   "SECOM CO., LTD." },
    { 0x0443,   "Tucker International LLC" },
    { 0x0444,   "Metanate Limited" },
    { 0x0445,   "Kobian Canada Inc." },
    { 0x0446,   "NETGEAR, Inc." },
    { 0x0447,   "Fabtronics Australia Pty Ltd" },
    { 0x0448,   "Grand Centrix GmbH" },
    { 0x0449,   "1UP USA.com llc" },
    { 0x044A,   "SHIMANO INC." },
    { 0x044B,   "Nain Inc." },
    { 0x044C,   "LifeStyle Lock, LLC" },
    { 0x044D,   "VEGA Grieshaber KG" },
    { 0x044E,   "Xtrava Inc." },
    { 0x044F,   "TTS Tooltechnic Systems AG & Co. KG" },
    { 0x0450,   "Teenage Engineering AB" },
    { 0x0451,   "Tunstall Nordic AB" },
    { 0x0452,   "Svep Design Center AB" },
    { 0x0453,   "Qorvo Utrecht B.V." },
    { 0x0454,   "Sphinx Electronics GmbH & Co KG" },
    { 0x0455,   "Atomation" },
    { 0x0456,   "Nemik Consulting Inc" },
    { 0x0457,   "RF INNOVATION" },
    { 0x0458,   "Mini Solution Co., Ltd." },
    { 0x0459,   "Lumenetix, Inc" },
    { 0x045A,   "2048450 Ontario Inc" },
    { 0x045B,   "SPACEEK LTD" },
    { 0x045C,   "Delta T Corporation" },
    { 0x045D,   "Boston Scientific Corporation" },
    { 0x045E,   "Nuviz, Inc." },
    { 0x045F,   "Real Time Automation, Inc." },
    { 0x0460,   "Kolibree" },
    { 0x0461,   "vhf elektronik GmbH" },
    { 0x0462,   "Bonsai Systems GmbH" },
    { 0x0463,   "Fathom Systems Inc." },
    { 0x0464,   "Bellman & Symfon Group AB" },
    { 0x0465,   "International Forte Group LLC" },
    { 0x0466,   "CycleLabs Solutions inc." },
    { 0x0467,   "Codenex Oy" },
    { 0x0468,   "Kynesim Ltd" },
    { 0x0469,   "Palago AB" },
    { 0x046A,   "INSIGMA INC." },
    { 0x046B,   "PMD Solutions" },
    { 0x046C,   "Qingdao Realtime Technology Co., Ltd." },
    { 0x046D,   "BEGA Gantenbrink-Leuchten KG" },
    { 0x046E,   "Pambor Ltd." },
    { 0x046F,   "Develco Products A/S" },
    { 0x0470,   "iDesign s.r.l." },
    { 0x0471,   "TiVo Corp" },
    { 0x0472,   "Control-J Pty Ltd" },
    { 0x0473,   "Steelcase, Inc." },
    { 0x0474,   "iApartment co., ltd." },
    { 0x0475,   "Icom inc." },
    { 0x0476,   "Oxstren Wearable Technologies Private Limited" },
    { 0x0477,   "Blue Spark Technologies" },
    { 0x0478,   "FarSite Communications Limited" },
    { 0x0479,   "mywerk system GmbH" },
    { 0x047A,   "Sinosun Technology Co., Ltd." },
    { 0x047B,   "MIYOSHI ELECTRONICS CORPORATION" },
    { 0x047C,   "POWERMAT LTD" },
    { 0x047D,   "Occly LLC" },
    { 0x047E,   "OurHub Dev IvS" },
    { 0x047F,   "Pro-Mark, Inc." },
    { 0x0480,   "Dynometrics Inc." },
    { 0x0481,   "Quintrax Limited" },
    { 0x0482,   "POS Tuning Udo Vosshenrich GmbH & Co. KG" },
    { 0x0483,   "Multi Care Systems B.V." },
    { 0x0484,   "Revol Technologies Inc" },
    { 0x0485,   "SKIDATA AG" },
    { 0x0486,   "DEV TECNOLOGIA INDUSTRIA, COMERCIO E MANUTENCAO DE EQUIPAMENTOS LTDA. - ME" },
    { 0x0487,   "Centrica Connected Home" },
    { 0x0488,   "Automotive Data Solutions Inc" },
    { 0x0489,   "Igarashi Engineering" },
    { 0x048A,   "Taelek Oy" },
    { 0x048B,   "CP Electronics Limited" },
    { 0x048C,   "Vectronix AG" },
    { 0x048D,   "S-Labs Sp. z o.o." },
    { 0x048E,   "Companion Medical, Inc." },
    { 0x048F,   "BlueKitchen GmbH" },
    { 0x0490,   "Matting AB" },
    { 0x0491,   "SOREX - Wireless Solutions GmbH" },
    { 0x0492,   "ADC Technology, Inc." },
    { 0x0493,   "Lynxemi Pte Ltd" },
    { 0x0494,   "SENNHEISER electronic GmbH & Co. KG" },
    { 0x0495,   "LMT Mercer Group, Inc" },
    { 0x0496,   "Polymorphic Labs LLC" },
    { 0x0497,   "Cochlear Limited" },
    { 0x0498,   "METER Group, Inc. USA" },
    { 0x0499,   "Ruuvi Innovations Ltd." },
    { 0x049A,   "Situne AS" },
    { 0x049B,   "nVisti, LLC" },
    { 0x049C,   "DyOcean" },
    { 0x049D,   "Uhlmann & Zacher GmbH" },
    { 0x049E,   "AND!XOR LLC" },
    { 0x049F,   "Popper Pay AB" },
    { 0x04A0,   "Vypin, LLC" },
    { 0x04A1,   "PNI Sensor Corporation" },
    { 0x04A2,   "ovrEngineered, LLC" },
    { 0x04A3,   "GT-tronics HK Ltd" },
    { 0x04A4,   "Herbert Waldmann GmbH & Co. KG" },
    { 0x04A5,   "Guangzhou FiiO Electronics Technology Co.,Ltd" },
    { 0x04A6,   "Vinetech Co., Ltd" },
    { 0x04A7,   "Dallas Logic Corporation" },
    { 0x04A8,   "BioTex, Inc." },
    { 0x04A9,   "DISCOVERY SOUND TECHNOLOGY, LLC" },
    { 0x04AA,   "LINKIO SAS" },
    { 0x04AB,   "Harbortronics, Inc." },
    { 0x04AC,   "Undagrid B.V." },
    { 0x04AD,   "Shure Inc" },
    { 0x04AE,   "ERM Electronic Systems LTD" },
    { 0x04AF,   "BIOROWER Handelsagentur GmbH" },
    { 0x04B0,   "Weba Sport und Med. Artikel GmbH" },
    { 0x04B1,   "Kartographers Technologies Pvt. Ltd." },
    { 0x04B2,   "The Shadow on the Moon" },
    { 0x04B3,   "mobike (Hong Kong) Limited" },
    { 0x04B4,   "Inuheat Group AB" },
    { 0x04B5,   "Swiftronix AB" },
    { 0x04B6,   "Diagnoptics Technologies" },
    { 0x04B7,   "Analog Devices, Inc." },
    { 0x04B8,   "Soraa Inc." },
    { 0x04B9,   "CSR Building Products Limited" },
    { 0x04BA,   "Crestron Electronics, Inc." },
    { 0x04BB,   "Neatebox Ltd" },
    { 0x04BC,   "Draegerwerk AG & Co. KGaA" },
    { 0x04BD,   "AlbynMedical" },
    { 0x04BE,   "Averos FZCO" },
    { 0x04BF,   "VIT Initiative, LLC" },
    { 0x04C0,   "Statsports International" },
    { 0x04C1,   "Sospitas, s.r.o." },
    { 0x04C2,   "Dmet Products Corp." },
    { 0x04C3,   "Mantracourt Electronics Limited" },
    { 0x04C4,   "TeAM Hutchins AB" },
    { 0x04C5,   "Seibert Williams Glass, LLC" },
    { 0x04C6,   "Insta GmbH" },
    { 0x04C7,   "Svantek Sp. z o.o." },
    { 0x04C8,   "Shanghai Flyco Electrical Appliance Co., Ltd." },
    { 0x04C9,   "Thornwave Labs Inc" },
    { 0x04CA,   "Steiner-Optik GmbH" },
    { 0x04CB,   "Novo Nordisk A/S" },
    { 0x04CC,   "Enflux Inc." },
    { 0x04CD,   "Safetech Products LLC" },
    { 0x04CE,   "GOOOLED S.R.L." },
    { 0x04CF,   "DOM Sicherheitstechnik GmbH & Co. KG" },
    { 0x04D0,   "Olympus Corporation" },
    { 0x04D1,   "KTS GmbH" },
    { 0x04D2,   "Anloq Technologies Inc." },
    { 0x04D3,   "Queercon, Inc" },
    { 0x04D4,   "5th Element Ltd" },
    { 0x04D5,   "Gooee Limited" },
    { 0x04D6,   "LUGLOC LLC" },
    { 0x04D7,   "Blincam, Inc." },
    { 0x04D8,   "FUJIFILM Corporation" },
    { 0x04D9,   "RM Acquisition LLC" },
    { 0x04DA,   "Franceschi Marina snc" },
    { 0x04DB,   "Engineered Audio, LLC." },
    { 0x04DC,   "IOTTIVE (OPC) PRIVATE LIMITED" },
    { 0x04DD,   "4MOD Technology" },
    { 0x04DE,   "Lutron Electronics Co., Inc." },
    { 0x04DF,   "Emerson Electric Co." },
    { 0x04E0,   "Guardtec, Inc." },
    { 0x04E1,   "REACTEC LIMITED" },
    { 0x04E2,   "EllieGrid" },
    { 0x04E3,   "Under Armour" },
    { 0x04E4,   "Woodenshark" },
    { 0x04E5,   "Avack Oy" },
    { 0x04E6,   "Smart Solution Technology, Inc." },
    { 0x04E7,   "REHABTRONICS INC." },
    { 0x04E8,   "STABILO International" },
    { 0x04E9,   "Busch Jaeger Elektro GmbH" },
    { 0x04EA,   "Pacific Bioscience Laboratories, Inc" },
    { 0x04EB,   "Bird Home Automation GmbH" },
    { 0x04EC,   "Motorola Solutions" },
    { 0x04ED,   "R9 Technology, Inc." },
    { 0x04EE,   "Auxivia" },
    { 0x04EF,   "DaisyWorks, Inc" },
    { 0x04F0,   "Kosi Limited" },
    { 0x04F1,   "Theben AG" },
    { 0x04F2,   "InDreamer Techsol Private Limited" },
    { 0x04F3,   "Cerevast Medical" },
    { 0x04F4,   "ZanCompute Inc." },
    { 0x04F5,   "Pirelli Tyre S.P.A." },
    { 0x04F6,   "McLear Limited" },
    { 0x04F7,   "Shenzhen Goodix Technology Co., Ltd" },
    { 0x04F8,   "Convergence Systems Limited" },
    { 0x04F9,   "Interactio" },
    { 0x04FA,   "Androtec GmbH" },
    { 0x04FB,   "Benchmark Drives GmbH & Co. KG" },
    { 0x04FC,   "SwingLync L. L. C." },
    { 0x04FD,   "Tapkey GmbH" },
    { 0x04FE,   "Woosim Systems Inc." },
    { 0x04FF,   "Microsemi Corporation" },
    { 0x0500,   "Wiliot LTD." },
    { 0x0501,   "Polaris IND" },
    { 0x0502,   "Specifi-Kali LLC" },
    { 0x0503,   "Locoroll, Inc" },
    { 0x0504,   "PHYPLUS Inc" },
    { 0x0505,   "InPlay, Inc." },
    { 0x0506,   "Hager" },
    { 0x0507,   "Yellowcog" },
    { 0x0508,   "Axes System sp. z o. o." },
    { 0x0509,   "Garage Smart, Inc." },
    { 0x050A,   "Shake-on B.V." },
    { 0x050B,   "Vibrissa Inc." },
    { 0x050C,   "OSRAM GmbH" },
    { 0x050D,   "TRSystems GmbH" },
    { 0x050E,   "Yichip Microelectronics (Hangzhou) Co.,Ltd." },
    { 0x050F,   "Foundation Engineering LLC" },
    { 0x0510,   "UNI-ELECTRONICS, INC." },
    { 0x0511,   "Brookfield Equinox LLC" },
    { 0x0512,   "Soprod SA" },
    { 0x0513,   "9974091 Canada Inc." },
    { 0x0514,   "FIBRO GmbH" },
    { 0x0515,   "RB Controls Co., Ltd." },
    { 0x0516,   "Footmarks" },
    { 0x0517,   "Amtronic Sverige AB" },
    { 0x0518,   "MAMORIO.inc" },
    { 0x0519,   "Tyto Life LLC" },
    { 0x051A,   "Leica Camera AG" },
    { 0x051B,   "Angee Technologies Ltd." },
    { 0x051C,   "EDPS" },
    { 0x051D,   "OFF Line Co., Ltd." },
    { 0x051E,   "Detect Blue Limited" },
    { 0x051F,   "Setec Pty Ltd" },
    { 0x0520,   "Target Corporation" },
    { 0x0521,   "IAI Corporation" },
    { 0x0522,   "NS Tech, Inc." },
    { 0x0523,   "MTG Co., Ltd." },
    { 0x0524,   "Hangzhou iMagic Technology Co., Ltd" },
    { 0x0525,   "HONGKONG NANO IC TECHNOLOGIES  CO., LIMITED" },
    { 0x0526,   "Honeywell International Inc." },
    { 0x0527,   "Albrecht JUNG" },
    { 0x0528,   "Lunera Lighting Inc." },
    { 0x0529,   "Lumen UAB" },
    { 0x052A,   "Keynes Controls Ltd" },
    { 0x052B,   "Novartis AG" },
    { 0x052C,   "Geosatis SA" },
    { 0x052D,   "EXFO, Inc." },
    { 0x052E,   "LEDVANCE GmbH" },
    { 0x052F,   "Center ID Corp." },
    { 0x0530,   "Adolene, Inc." },
    { 0x0531,   "D&M Holdings Inc." },
    { 0x0532,   "CRESCO Wireless, Inc." },
    { 0x0533,   "Nura Operations Pty Ltd" },
    { 0x0534,   "Frontiergadget, Inc." },
    { 0x0535,   "Smart Component Technologies Limited" },
    { 0x0536,   "ZTR Control Systems LLC" },
    { 0x0537,   "MetaLogics Corporation" },
    { 0x0538,   "Medela AG" },
    { 0x0539,   "OPPLE Lighting Co., Ltd" },
    { 0x053A,   "Savitech Corp.," },
    { 0x053B,   "prodigy" },
    { 0x053C,   "Screenovate Technologies Ltd" },
    { 0x053D,   "TESA SA" },
    { 0x053E,   "CLIM8 LIMITED" },
    { 0x053F,   "Silergy Corp" },
    { 0x0540,   "SilverPlus, Inc" },
    { 0x0541,   "Sharknet srl" },
    { 0x0542,   "Mist Systems, Inc." },
    { 0x0543,   "MIWA LOCK CO.,Ltd" },
    { 0x0544,   "OrthoSensor, Inc." },
    { 0x0545,   "Candy Hoover Group s.r.l" },
    { 0x0546,   "Apexar Technologies S.A." },
    { 0x0547,   "LOGICDATA Electronic & Software Entwicklungs GmbH" },
    { 0x0548,   "Knick Elektronische Messgeraete GmbH & Co. KG" },
    { 0x0549,   "Smart Technologies and Investment Limited" },
    { 0x054A,   "Linough Inc." },
    { 0x054B,   "Advanced Electronic Designs, Inc." },
    { 0x054C,   "Carefree Scott Fetzer Co Inc" },
    { 0x054D,   "Sensome" },
    { 0x054E,   "FORTRONIK storitve d.o.o." },
    { 0x054F,   "Sinnoz" },
    { 0x0550,   "Versa Networks, Inc." },
    { 0x0551,   "Sylero" },
    { 0x0552,   "Avempace SARL" },
    { 0x0553,   "Nintendo Co., Ltd." },
    { 0x0554,   "National Instruments" },
    { 0x0555,   "KROHNE Messtechnik GmbH" },
    { 0x0556,   "Otodynamics Ltd" },
    { 0x0557,   "Arwin Technology Limited" },
    { 0x0558,   "benegear, inc." },
    { 0x0559,   "Newcon Optik" },
    { 0x055A,   "CANDY HOUSE, Inc." },
    { 0x055B,   "FRANKLIN TECHNOLOGY INC" },
    { 0x055C,   "Lely" },
    { 0x055D,   "Valve Corporation" },
    { 0x055E,   "Hekatron Vertriebs GmbH" },
    { 0x055F,   "PROTECH S.A.S. DI GIRARDI ANDREA & C." },
    { 0x0560,   "Sarita CareTech APS" },
    { 0x0561,   "Finder S.p.A." },
    { 0x0562,   "Thalmic Labs Inc." },
    { 0x0563,   "Steinel Vertrieb GmbH" },
    { 0x0564,   "Beghelli Spa" },
    { 0x0565,   "Beijing Smartspace Technologies Inc." },
    { 0x0566,   "CORE TRANSPORT TECHNOLOGIES NZ LIMITED" },
    { 0x0567,   "Xiamen Everesports Goods Co., Ltd" },
    { 0x0568,   "Bodyport Inc." },
    { 0x0569,   "Audionics System, INC." },
    { 0x056A,   "Flipnavi Co.,Ltd." },
    { 0x056B,   "Rion Co., Ltd." },
    { 0x056C,   "Long Range Systems, LLC" },
    { 0x056D,   "Redmond Industrial Group LLC" },
    { 0x056E,   "VIZPIN INC." },
    { 0x056F,   "BikeFinder AS" },
    { 0x0570,   "Consumer Sleep Solutions LLC" },
    { 0x0571,   "PSIKICK, INC." },
    { 0x0572,   "AntTail.com" },
    { 0x0573,   "Lighting Science Group Corp." },
    { 0x0574,   "AFFORDABLE ELECTRONICS INC" },
    { 0x0575,   "Integral Memroy Plc" },
    { 0x0576,   "Globalstar, Inc." },
    { 0x0577,   "True Wearables, Inc." },
    { 0x0578,   "Wellington Drive Technologies Ltd" },
    { 0x0579,   "Ensemble Tech Private Limited" },
    { 0x057A,   "OMNI Remotes" },
    { 0x057B,   "Duracell U.S. Operations Inc." },
    { 0x057C,   "Toor Technologies LLC" },
    { 0x057D,   "Instinct Performance" },
    { 0x057E,   "Beco, Inc" },
    { 0x057F,   "Scuf Gaming International, LLC" },
    { 0x0580,   "ARANZ Medical Limited" },
    { 0x0581,   "LYS TECHNOLOGIES LTD" },
    { 0x0582,   "Breakwall Analytics, LLC" },
    { 0x0583,   "Code Blue Communications" },
    { 0x0584,   "Gira Giersiepen GmbH & Co. KG" },
    { 0x0585,   "Hearing Lab Technology" },
    { 0x0586,   "LEGRAND" },
    { 0x0587,   "Derichs GmbH" },
    { 0x0588,   "ALT-TEKNIK LLC" },
    { 0x0589,   "Star Technologies" },
    { 0x058A,   "START TODAY CO.,LTD." },
    { 0x058B,   "Maxim Integrated Products" },
    { 0x058C,   "Fracarro Radioindustrie SRL" },
    { 0x058D,   "Jungheinrich Aktiengesellschaft" },
    { 0x058E,   "Meta Platforms Technologies, LLC" },
    { 0x058F,   "HENDON SEMICONDUCTORS PTY LTD" },
    { 0x0590,   "Pur3 Ltd" },
    { 0x0591,   "Viasat Group S.p.A." },
    { 0x0592,   "IZITHERM" },
    { 0x0593,   "Spaulding Clinical Research" },
    { 0x0594,   "Kohler Company" },
    { 0x0595,   "Inor Process AB" },
    { 0x0596,   "My Smart Blinds" },
    { 0x0597,   "RadioPulse Inc" },
    { 0x0598,   "rapitag GmbH" },
    { 0x0599,   "Lazlo326, LLC." },
    { 0x059A,   "Teledyne Lecroy, Inc." },
    { 0x059B,   "Dataflow Systems Limited" },
    { 0x059C,   "Macrogiga Electronics" },
    { 0x059D,   "Tandem Diabetes Care" },
    { 0x059E,   "Polycom, Inc." },
    { 0x059F,   "Fisher & Paykel Healthcare" },
    { 0x05A0,   "RCP Software Oy" },
    { 0x05A1,   "Shanghai Xiaoyi Technology Co.,Ltd." },
    { 0x05A2,   "ADHERIUM(NZ) LIMITED" },
    { 0x05A3,   "Axiomware Systems Incorporated" },
    { 0x05A4,   "O. E. M. Controls, Inc." },
    { 0x05A5,   "Kiiroo BV" },
    { 0x05A6,   "Telecon Mobile Limited" },
    { 0x05A7,   "Sonos Inc" },
    { 0x05A8,   "Tom Allebrandi Consulting" },
    { 0x05A9,   "Monidor" },
    { 0x05AA,   "Tramex Limited" },
    { 0x05AB,   "Nofence AS" },
    { 0x05AC,   "GoerTek Dynaudio Co., Ltd." },
    { 0x05AD,   "INIA" },
    { 0x05AE,   "CARMATE MFG.CO.,LTD" },
    { 0x05AF,   "OV LOOP, INC." },
    { 0x05B0,   "NewTec GmbH" },
    { 0x05B1,   "Medallion Instrumentation Systems" },
    { 0x05B2,   "CAREL INDUSTRIES S.P.A." },
    { 0x05B3,   "Parabit Systems, Inc." },
    { 0x05B4,   "White Horse Scientific ltd" },
    { 0x05B5,   "verisilicon" },
    { 0x05B6,   "Elecs Industry Co.,Ltd." },
    { 0x05B7,   "Beijing Pinecone Electronics Co.,Ltd." },
    { 0x05B8,   "Ambystoma Labs Inc." },
    { 0x05B9,   "Suzhou Pairlink Network Technology" },
    { 0x05BA,   "igloohome" },
    { 0x05BB,   "Oxford Metrics plc" },
    { 0x05BC,   "Leviton Mfg. Co., Inc." },
    { 0x05BD,   "ULC Robotics Inc." },
    { 0x05BE,   "RFID Global by Softwork SrL" },
    { 0x05BF,   "Real-World-Systems Corporation" },
    { 0x05C0,   "Nalu Medical, Inc." },
    { 0x05C1,   "P.I.Engineering" },
    { 0x05C2,   "Grote Industries" },
    { 0x05C3,   "Runtime, Inc." },
    { 0x05C4,   "Codecoup sp. z o.o. sp. k." },
    { 0x05C5,   "SELVE GmbH & Co. KG" },
    { 0x05C6,   "Smart Animal Training Systems, LLC" },
    { 0x05C7,   "Lippert Components, INC" },
    { 0x05C8,   "SOMFY SAS" },
    { 0x05C9,   "TBS Electronics B.V." },
    { 0x05CA,   "MHL Custom Inc" },
    { 0x05CB,   "LucentWear LLC" },
    { 0x05CC,   "WATTS ELECTRONICS" },
    { 0x05CD,   "RJ Brands LLC" },
    { 0x05CE,   "V-ZUG Ltd" },
    { 0x05CF,   "Biowatch SA" },
    { 0x05D0,   "Anova Applied Electronics" },
    { 0x05D1,   "Lindab AB" },
    { 0x05D2,   "frogblue TECHNOLOGY GmbH" },
    { 0x05D3,   "Acurable Limited" },
    { 0x05D4,   "LAMPLIGHT Co., Ltd." },
    { 0x05D5,   "TEGAM, Inc." },
    { 0x05D6,   "Zhuhai Jieli technology Co.,Ltd" },
    { 0x05D7,   "modum.io AG" },
    { 0x05D8,   "Farm Jenny LLC" },
    { 0x05D9,   "Toyo Electronics Corporation" },
    { 0x05DA,   "Applied Neural Research Corp" },
    { 0x05DB,   "Avid Identification Systems, Inc." },
    { 0x05DC,   "Petronics Inc." },
    { 0x05DD,   "essentim GmbH" },
    { 0x05DE,   "QT Medical INC." },
    { 0x05DF,   "VIRTUALCLINIC.DIRECT LIMITED" },
    { 0x05E0,   "Viper Design LLC" },
    { 0x05E1,   "Human, Incorporated" },
    { 0x05E2,   "stAPPtronics GmbH" },
    { 0x05E3,   "Elemental Machines, Inc." },
    { 0x05E4,   "Taiyo Yuden Co., Ltd" },
    { 0x05E5,   "INEO ENERGY& SYSTEMS" },
    { 0x05E6,   "Motion Instruments Inc." },
    { 0x05E7,   "PressurePro" },
    { 0x05E8,   "COWBOY" },
    { 0x05E9,   "iconmobile GmbH" },
    { 0x05EA,   "ACS-Control-System GmbH" },
    { 0x05EB,   "Bayerische Motoren Werke AG" },
    { 0x05EC,   "Gycom Svenska AB" },
    { 0x05ED,   "Fuji Xerox Co., Ltd" },
    { 0x05EE,   "Wristcam Inc." },
    { 0x05EF,   "SIKOM AS" },
    { 0x05F0,   "beken" },
    { 0x05F1,   "The Linux Foundation" },
    { 0x05F2,   "Try and E CO.,LTD." },
    { 0x05F3,   "SeeScan" },
    { 0x05F4,   "Clearity, LLC" },
    { 0x05F5,   "GS TAG" },
    { 0x05F6,   "DPTechnics" },
    { 0x05F7,   "TRACMO, INC." },
    { 0x05F8,   "Anki Inc." },
    { 0x05F9,   "Hagleitner Hygiene International GmbH" },
    { 0x05FA,   "Konami Sports Life Co., Ltd." },
    { 0x05FB,   "Arblet Inc." },
    { 0x05FC,   "Masbando GmbH" },
    { 0x05FD,   "Innoseis" },
    { 0x05FE,   "Niko nv" },
    { 0x05FF,   "Wellnomics Ltd" },
    { 0x0600,   "iRobot Corporation" },
    { 0x0601,   "Schrader Electronics" },
    { 0x0602,   "Geberit International AG" },
    { 0x0603,   "Fourth Evolution Inc" },
    { 0x0604,   "Cell2Jack LLC" },
    { 0x0605,   "FMW electronic Futterer u. Maier-Wolf OHG" },
    { 0x0606,   "John Deere" },
    { 0x0607,   "Rookery Technology Ltd" },
    { 0x0608,   "KeySafe-Cloud" },
    { 0x0609,   "BUCHI Labortechnik AG" },
    { 0x060A,   "IQAir AG" },
    { 0x060B,   "Triax Technologies Inc" },
    { 0x060C,   "Vuzix Corporation" },
    { 0x060D,   "TDK Corporation" },
    { 0x060E,   "Blueair AB" },
    { 0x060F,   "Signify Netherlands B.V." },
    { 0x0610,   "ADH GUARDIAN USA LLC" },
    { 0x0611,   "Beurer GmbH" },
    { 0x0612,   "Playfinity AS" },
    { 0x0613,   "Hans Dinslage GmbH" },
    { 0x0614,   "OnAsset Intelligence, Inc." },
    { 0x0615,   "INTER ACTION Corporation" },
    { 0x0616,   "OS42 UG (haftungsbeschraenkt)" },
    { 0x0617,   "WIZCONNECTED COMPANY LIMITED" },
    { 0x0618,   "Audio-Technica Corporation" },
    { 0x0619,   "Six Guys Labs, s.r.o." },
    { 0x061A,   "R.W. Beckett Corporation" },
    { 0x061B,   "silex technology, inc." },
    { 0x061C,   "Univations Limited" },
    { 0x061D,   "SENS Innovation ApS" },
    { 0x061E,   "Diamond Kinetics, Inc." },
    { 0x061F,   "Phrame Inc." },
    { 0x0620,   "Forciot Oy" },
    { 0x0621,   "Noordung d.o.o." },
    { 0x0622,   "Beam Labs, LLC" },
    { 0x0623,   "Philadelphia Scientific (U.K.) Limited" },
    { 0x0624,   "Biovotion AG" },
    { 0x0625,   "Square Panda, Inc." },
    { 0x0626,   "Amplifico" },
    { 0x0627,   "WEG S.A." },
    { 0x0628,   "Ensto Oy" },
    { 0x0629,   "PHONEPE PVT LTD" },
    { 0x062A,   "Lunatico Astronomia SL" },
    { 0x062B,   "MinebeaMitsumi Inc." },
    { 0x062C,   "ASPion GmbH" },
    { 0x062D,   "Vossloh-Schwabe Deutschland GmbH" },
    { 0x062E,   "Procept" },
    { 0x062F,   "ONKYO Corporation" },
    { 0x0630,   "Asthrea D.O.O." },
    { 0x0631,   "Fortiori Design LLC" },
    { 0x0632,   "Hugo Muller GmbH & Co KG" },
    { 0x0633,   "Wangi Lai PLT" },
    { 0x0634,   "Fanstel Corp" },
    { 0x0635,   "Crookwood" },
    { 0x0636,   "ELECTRONICA INTEGRAL DE SONIDO S.A." },
    { 0x0637,   "GiP Innovation Tools GmbH" },
    { 0x0638,   "LX SOLUTIONS PTY LIMITED" },
    { 0x0639,   "Shenzhen Minew Technologies Co., Ltd." },
    { 0x063A,   "Prolojik Limited" },
    { 0x063B,   "Kromek Group Plc" },
    { 0x063C,   "Contec Medical Systems Co., Ltd." },
    { 0x063D,   "Xradio Technology Co.,Ltd." },
    { 0x063E,   "The Indoor Lab, LLC" },
    { 0x063F,   "LDL TECHNOLOGY" },
    { 0x0640,   "Dish Network LLC" },
    { 0x0641,   "Revenue Collection Systems FRANCE SAS" },
    { 0x0642,   "Bluetrum Technology Co.,Ltd" },
    { 0x0643,   "makita corporation" },
    { 0x0644,   "Apogee Instruments" },
    { 0x0645,   "BM3" },
    { 0x0646,   "SGV Group Holding GmbH & Co. KG" },
    { 0x0647,   "MED-EL" },
    { 0x0648,   "Ultune Technologies" },
    { 0x0649,   "Ryeex Technology Co.,Ltd." },
    { 0x064A,   "Open Research Institute, Inc." },
    { 0x064B,   "Scale-Tec, Ltd" },
    { 0x064C,   "Zumtobel Group AG" },
    { 0x064D,   "iLOQ Oy" },
    { 0x064E,   "KRUXWorks Technologies Private Limited" },
    { 0x064F,   "Digital Matter Pty Ltd" },
    { 0x0650,   "Coravin, Inc." },
    { 0x0651,   "Stasis Labs, Inc." },
    { 0x0652,   "ITZ Innovations- und Technologiezentrum GmbH" },
    { 0x0653,   "Meggitt SA" },
    { 0x0654,   "Ledlenser GmbH & Co. KG" },
    { 0x0655,   "Renishaw PLC" },
    { 0x0656,   "ZhuHai AdvanPro Technology Company Limited" },
    { 0x0657,   "Meshtronix Limited" },
    { 0x0658,   "Payex Norge AS" },
    { 0x0659,   "UnSeen Technologies Oy" },
    { 0x065A,   "Zound Industries International AB" },
    { 0x065B,   "Sesam Solutions BV" },
    { 0x065C,   "PixArt Imaging Inc." },
    { 0x065D,   "Panduit Corp." },
    { 0x065E,   "Alo AB" },
    { 0x065F,   "Ricoh Company Ltd" },
    { 0x0660,   "RTC Industries, Inc." },
    { 0x0661,   "Mode Lighting Limited" },
    { 0x0662,   "Particle Industries, Inc." },
    { 0x0663,   "Advanced Telemetry Systems, Inc." },
    { 0x0664,   "RHA TECHNOLOGIES LTD" },
    { 0x0665,   "Pure International Limited" },
    { 0x0666,   "WTO Werkzeug-Einrichtungen GmbH" },
    { 0x0667,   "Spark Technology Labs Inc." },
    { 0x0668,   "Bleb Technology srl" },
    { 0x0669,   "Livanova USA, Inc." },
    { 0x066A,   "Brady Worldwide Inc." },
    { 0x066B,   "DewertOkin GmbH" },
    { 0x066C,   "Ztove ApS" },
    { 0x066D,   "Venso EcoSolutions AB" },
    { 0x066E,   "Eurotronik Kranj d.o.o." },
    { 0x066F,   "Hug Technology Ltd" },
    { 0x0670,   "Gema Switzerland GmbH" },
    { 0x0671,   "Buzz Products Ltd." },
    { 0x0672,   "Kopi" },
    { 0x0673,   "Innova Ideas Limited" },
    { 0x0674,   "BeSpoon" },
    { 0x0675,   "Deco Enterprises, Inc." },
    { 0x0676,   "Expai Solutions Private Limited" },
    { 0x0677,   "Innovation First, Inc." },
    { 0x0678,   "SABIK Offshore GmbH" },
    { 0x0679,   "4iiii Innovations Inc." },
    { 0x067A,   "The Energy Conservatory, Inc." },
    { 0x067B,   "I.FARM, INC." },
    { 0x067C,   "Tile, Inc." },
    { 0x067D,   "Form Athletica Inc." },
    { 0x067E,   "MbientLab Inc" },
    { 0x067F,   "NETGRID S.N.C. DI BISSOLI MATTEO, CAMPOREALE SIMONE, TOGNETTI FEDERICO" },
    { 0x0680,   "Mannkind Corporation" },
    { 0x0681,   "Trade FIDES a.s." },
    { 0x0682,   "Photron Limited" },
    { 0x0683,   "Eltako GmbH" },
    { 0x0684,   "Dermalapps, LLC" },
    { 0x0685,   "Greenwald Industries" },
    { 0x0686,   "inQs Co., Ltd." },
    { 0x0687,   "Cherry GmbH" },
    { 0x0688,   "Amsted Digital Solutions Inc." },
    { 0x0689,   "Tacx b.v." },
    { 0x068A,   "Raytac Corporation" },
    { 0x068B,   "Jiangsu Teranovo Tech Co., Ltd." },
    { 0x068C,   "Changzhou Sound Dragon Electronics and Acoustics Co., Ltd" },
    { 0x068D,   "JetBeep Inc." },
    { 0x068E,   "Razer Inc." },
    { 0x068F,   "JRM Group Limited" },
    { 0x0690,   "Eccrine Systems, Inc." },
    { 0x0691,   "Curie Point AB" },
    { 0x0692,   "Georg Fischer AG" },
    { 0x0693,   "Hach - Danaher" },
    { 0x0694,   "T&A Laboratories LLC" },
    { 0x0695,   "Koki Holdings Co., Ltd." },
    { 0x0696,   "Gunakar Private Limited" },
    { 0x0697,   "Stemco Products Inc" },
    { 0x0698,   "Wood IT Security, LLC" },
    { 0x0699,   "RandomLab SAS" },
    { 0x069A,   "Adero, Inc." },
    { 0x069B,   "Dragonchip Limited" },
    { 0x069C,   "Noomi AB" },
    { 0x069D,   "Vakaros LLC" },
    { 0x069E,   "Delta Electronics, Inc." },
    { 0x069F,   "FlowMotion Technologies AS" },
    { 0x06A0,   "OBIQ Location Technology Inc." },
    { 0x06A1,   "Cardo Systems, Ltd" },
    { 0x06A2,   "Globalworx GmbH" },
    { 0x06A3,   "Nymbus, LLC" },
    { 0x06A4,   "LIMNO Co. Ltd." },
    { 0x06A5,   "TEKZITEL PTY LTD" },
    { 0x06A6,   "Roambee Corporation" },
    { 0x06A7,   "Chipsea Technologies (ShenZhen) Corp." },
    { 0x06A8,   "GD Midea Air-Conditioning Equipment Co., Ltd." },
    { 0x06A9,   "Soundmax Electronics Limited" },
    { 0x06AA,   "Produal Oy" },
    { 0x06AB,   "HMS Industrial Networks AB" },
    { 0x06AC,   "Ingchips Technology Co., Ltd." },
    { 0x06AD,   "InnovaSea Systems Inc." },
    { 0x06AE,   "SenseQ Inc." },
    { 0x06AF,   "Shoof Technologies" },
    { 0x06B0,   "BRK Brands, Inc." },
    { 0x06B1,   "SimpliSafe, Inc." },
    { 0x06B2,   "Tussock Innovation 2013 Limited" },
    { 0x06B3,   "The Hablab ApS" },
    { 0x06B4,   "Sencilion Oy" },
    { 0x06B5,   "Wabilogic Ltd." },
    { 0x06B6,   "Sociometric Solutions, Inc." },
    { 0x06B7,   "iCOGNIZE GmbH" },
    { 0x06B8,   "ShadeCraft, Inc" },
    { 0x06B9,   "Beflex Inc." },
    { 0x06BA,   "Beaconzone Ltd" },
    { 0x06BB,   "Leaftronix Analogic Solutions Private Limited" },
    { 0x06BC,   "TWS Srl" },
    { 0x06BD,   "ABB Oy" },
    { 0x06BE,   "HitSeed Oy" },
    { 0x06BF,   "Delcom Products Inc." },
    { 0x06C0,   "CAME S.p.A." },
    { 0x06C1,   "Alarm.com Holdings, Inc" },
    { 0x06C2,   "Measurlogic Inc." },
    { 0x06C3,   "King I Electronics.Co.,Ltd" },
    { 0x06C4,   "Dream Labs GmbH" },
    { 0x06C5,   "Urban Compass, Inc" },
    { 0x06C6,   "Simm Tronic Limited" },
    { 0x06C7,   "Somatix Inc" },
    { 0x06C8,   "Storz & Bickel GmbH & Co. KG" },
    { 0x06C9,   "MYLAPS B.V." },
    { 0x06CA,   "Shenzhen Zhongguang Infotech Technology Development Co., Ltd" },
    { 0x06CB,   "Dyeware, LLC" },
    { 0x06CC,   "Dongguan SmartAction Technology Co.,Ltd." },
    { 0x06CD,   "DIG Corporation" },
    { 0x06CE,   "FIOR & GENTZ" },
    { 0x06CF,   "Belparts N.V." },
    { 0x06D0,   "Etekcity Corporation" },
    { 0x06D1,   "Meyer Sound Laboratories, Incorporated" },
    { 0x06D2,   "CeoTronics AG" },
    { 0x06D3,   "TriTeq Lock and Security, LLC" },
    { 0x06D4,   "DYNAKODE TECHNOLOGY PRIVATE LIMITED" },
    { 0x06D5,   "Sensirion AG" },
    { 0x06D6,   "JCT Healthcare Pty Ltd" },
    { 0x06D7,   "FUBA Automotive Electronics GmbH" },
    { 0x06D8,   "AW Company" },
    { 0x06D9,   "Shanghai Mountain View Silicon Co.,Ltd." },
    { 0x06DA,   "Zliide Technologies ApS" },
    { 0x06DB,   "Automatic Labs, Inc." },
    { 0x06DC,   "Industrial Network Controls, LLC" },
    { 0x06DD,   "Intellithings Ltd." },
    { 0x06DE,   "Navcast, Inc." },
    { 0x06DF,   "HLI Solutions Inc." },
    { 0x06E0,   "Avaya Inc." },
    { 0x06E1,   "Milestone AV Technologies LLC" },
    { 0x06E2,   "Alango Technologies Ltd" },
    { 0x06E3,   "Spinlock Ltd" },
    { 0x06E4,   "Aluna" },
    { 0x06E5,   "OPTEX CO.,LTD." },
    { 0x06E6,   "NIHON DENGYO KOUSAKU" },
    { 0x06E7,   "VELUX A/S" },
    { 0x06E8,   "Almendo Technologies GmbH" },
    { 0x06E9,   "Zmartfun Electronics, Inc." },
    { 0x06EA,   "SafeLine Sweden AB" },
    { 0x06EB,   "Houston Radar LLC" },
    { 0x06EC,   "Sigur" },
    { 0x06ED,   "J Neades Ltd" },
    { 0x06EE,   "Avantis Systems Limited" },
    { 0x06EF,   "ALCARE Co., Ltd." },
    { 0x06F0,   "Chargy Technologies, SL" },
    { 0x06F1,   "Shibutani Co., Ltd." },
    { 0x06F2,   "Trapper Data AB" },
    { 0x06F3,   "Alfred International Inc." },
    { 0x06F4,   "Touché Technology Ltd" },
    { 0x06F5,   "Vigil Technologies Inc." },
    { 0x06F6,   "Vitulo Plus BV" },
    { 0x06F7,   "WILKA Schliesstechnik GmbH" },
    { 0x06F8,   "BodyPlus Technology Co.,Ltd" },
    { 0x06F9,   "happybrush GmbH" },
    { 0x06FA,   "Enequi AB" },
    { 0x06FB,   "Sartorius AG" },
    { 0x06FC,   "Tom Communication Industrial Co.,Ltd." },
    { 0x06FD,   "ESS Embedded System Solutions Inc." },
    { 0x06FE,   "Mahr GmbH" },
    { 0x06FF,   "Redpine Signals Inc" },
    { 0x0700,   "TraqFreq LLC" },
    { 0x0701,   "PAFERS TECH" },
    { 0x0702,   "Akciju sabiedriba \"SAF TEHNIKA\"" },
    { 0x0703,   "Beijing Jingdong Century Trading Co., Ltd." },
    { 0x0704,   "JBX Designs Inc." },
    { 0x0705,   "AB Electrolux" },
    { 0x0706,   "Wernher von Braun Center for ASdvanced Research" },
    { 0x0707,   "Essity Hygiene and Health Aktiebolag" },
    { 0x0708,   "Be Interactive Co., Ltd" },
    { 0x0709,   "Carewear Corp." },
    { 0x070A,   "Huf Hülsbeck & Fürst GmbH & Co. KG" },
    { 0x070B,   "Element Products, Inc." },
    { 0x070C,   "Beijing Winner Microelectronics Co.,Ltd" },
    { 0x070D,   "SmartSnugg Pty Ltd" },
    { 0x070E,   "FiveCo Sarl" },
    { 0x070F,   "California Things Inc." },
    { 0x0710,   "Audiodo AB" },
    { 0x0711,   "ABAX AS" },
    { 0x0712,   "Bull Group Company Limited" },
    { 0x0713,   "Respiri Limited" },
    { 0x0714,   "MindPeace Safety LLC" },
    { 0x0715,   "MBARC LABS Inc" },
    { 0x0716,   "Altonics" },
    { 0x0717,   "iQsquare BV" },
    { 0x0718,   "IDIBAIX enginneering" },
    { 0x0719,   "COREIOT PTY LTD" },
    { 0x071A,   "REVSMART WEARABLE HK CO LTD" },
    { 0x071B,   "Precor" },
    { 0x071C,   "F5 Sports, Inc" },
    { 0x071D,   "exoTIC Systems" },
    { 0x071E,   "DONGGUAN HELE ELECTRONICS CO., LTD" },
    { 0x071F,   "Dongguan Liesheng Electronic Co.Ltd" },
    { 0x0720,   "Oculeve, Inc." },
    { 0x0721,   "Clover Network, Inc." },
    { 0x0722,   "Xiamen Eholder Electronics Co.Ltd" },
    { 0x0723,   "Ford Motor Company" },
    { 0x0724,   "Guangzhou SuperSound Information Technology Co.,Ltd" },
    { 0x0725,   "Tedee Sp. z o.o." },
    { 0x0726,   "PHC Corporation" },
    { 0x0727,   "STALKIT AS" },
    { 0x0728,   "Eli Lilly and Company" },
    { 0x0729,   "SwaraLink Technologies" },
    { 0x072A,   "JMR embedded systems GmbH" },
    { 0x072B,   "Bitkey Inc." },
    { 0x072C,   "GWA Hygiene GmbH" },
    { 0x072D,   "Safera Oy" },
    { 0x072E,   "Open Platform Systems LLC" },
    { 0x072F,   "OnePlus Electronics (Shenzhen) Co., Ltd." },
    { 0x0730,   "Wildlife Acoustics, Inc." },
    { 0x0731,   "ABLIC Inc." },
    { 0x0732,   "Dairy Tech, Inc." },
    { 0x0733,   "Iguanavation, Inc." },
    { 0x0734,   "DiUS Computing Pty Ltd" },
    { 0x0735,   "UpRight Technologies LTD" },
    { 0x0736,   "Luna XIO, Inc." },
    { 0x0737,   "LLC Navitek" },
    { 0x0738,   "Glass Security Pte Ltd" },
    { 0x0739,   "Jiangsu Qinheng Co., Ltd." },
    { 0x073A,   "Chandler Systems Inc." },
    { 0x073B,   "Fantini Cosmi s.p.a." },
    { 0x073C,   "Acubit ApS" },
    { 0x073D,   "Beijing Hao Heng Tian Tech Co., Ltd." },
    { 0x073E,   "Bluepack S.R.L." },
    { 0x073F,   "Beijing Unisoc Technologies Co., Ltd." },
    { 0x0740,   "HITIQ LIMITED" },
    { 0x0741,   "MAC SRL" },
    { 0x0742,   "DML LLC" },
    { 0x0743,   "Sanofi" },
    { 0x0744,   "SOCOMEC" },
    { 0x0745,   "WIZNOVA, Inc." },
    { 0x0746,   "Seitec Elektronik GmbH" },
    { 0x0747,   "OR Technologies Pty Ltd" },
    { 0x0748,   "GuangZhou KuGou Computer Technology Co.Ltd" },
    { 0x0749,   "DIAODIAO (Beijing) Technology Co., Ltd." },
    { 0x074A,   "Illusory Studios LLC" },
    { 0x074B,   "Sarvavid Software Solutions LLP" },
    { 0x074C,   "iopool s.a." },
    { 0x074D,   "Amtech Systems, LLC" },
    { 0x074E,   "EAGLE DETECTION SA" },
    { 0x074F,   "MEDIATECH S.R.L." },
    { 0x0750,   "Hamilton Professional Services of Canada Incorporated" },
    { 0x0751,   "Changsha JEMO IC Design Co.,Ltd" },
    { 0x0752,   "Elatec GmbH" },
    { 0x0753,   "JLG Industries, Inc." },
    { 0x0754,   "Michael Parkin" },
    { 0x0755,   "Brother Industries, Ltd" },
    { 0x0756,   "Lumens For Less, Inc" },
    { 0x0757,   "ELA Innovation" },
    { 0x0758,   "umanSense AB" },
    { 0x0759,   "Shanghai InGeek Cyber Security Co., Ltd." },
    { 0x075A,   "HARMAN CO.,LTD." },
    { 0x075B,   "Smart Sensor Devices AB" },
    { 0x075C,   "Antitronics Inc." },
    { 0x075D,   "RHOMBUS SYSTEMS, INC." },
    { 0x075E,   "Katerra Inc." },
    { 0x075F,   "Remote Solution Co., LTD." },
    { 0x0760,   "Vimar SpA" },
    { 0x0761,   "Mantis Tech LLC" },
    { 0x0762,   "TerOpta Ltd" },
    { 0x0763,   "PIKOLIN S.L." },
    { 0x0764,   "WWZN Information Technology Company Limited" },
    { 0x0765,   "Voxx International" },
    { 0x0766,   "ART AND PROGRAM, INC." },
    { 0x0767,   "NITTO DENKO ASIA TECHNICAL CENTRE PTE. LTD." },
    { 0x0768,   "Peloton Interactive Inc." },
    { 0x0769,   "Force Impact Technologies" },
    { 0x076A,   "Dmac Mobile Developments, LLC" },
    { 0x076B,   "Engineered Medical Technologies" },
    { 0x076C,   "Noodle Technology inc" },
    { 0x076D,   "Graesslin GmbH" },
    { 0x076E,   "WuQi technologies, Inc." },
    { 0x076F,   "Successful Endeavours Pty Ltd" },
    { 0x0770,   "InnoCon Medical ApS" },
    { 0x0771,   "Corvex Connected Safety" },
    { 0x0772,   "Thirdwayv Inc." },
    { 0x0773,   "Echoflex Solutions Inc." },
    { 0x0774,   "C-MAX Asia Limited" },
    { 0x0775,   "4eBusiness GmbH" },
    { 0x0776,   "Cyber Transport Control GmbH" },
    { 0x0777,   "Cue" },
    { 0x0778,   "KOAMTAC INC." },
    { 0x0779,   "Loopshore Oy" },
    { 0x077A,   "Niruha Systems Private Limited" },
    { 0x077B,   "AmaterZ, Inc." },
    { 0x077C,   "radius co., ltd." },
    { 0x077D,   "Sensority, s.r.o." },
    { 0x077E,   "Sparkage Inc." },
    { 0x077F,   "Glenview Software Corporation" },
    { 0x0780,   "Finch Technologies Ltd." },
    { 0x0781,   "Qingping Technology (Beijing) Co., Ltd." },
    { 0x0782,   "DeviceDrive AS" },
    { 0x0783,   "ESEMBER LIMITED LIABILITY COMPANY" },
    { 0x0784,   "audifon GmbH & Co. KG" },
    { 0x0785,   "O2 Micro, Inc." },
    { 0x0786,   "HLP Controls Pty Limited" },
    { 0x0787,   "Pangaea Solution" },
    { 0x0788,   "BubblyNet, LLC" },
    { 0x0789,   "PCB Piezotronics, Inc." },
    { 0x078A,   "The Wildflower Foundation" },
    { 0x078B,   "Optikam Tech Inc." },
    { 0x078C,   "MINIBREW HOLDING B.V" },
    { 0x078D,   "Cybex GmbH" },
    { 0x078E,   "FUJIMIC NIIGATA, INC." },
    { 0x078F,   "Hanna Instruments, Inc." },
    { 0x0790,   "KOMPAN A/S" },
    { 0x0791,   "Scosche Industries, Inc." },
    { 0x0792,   "Cricut, Inc." },
    { 0x0793,   "AEV spol. s r.o." },
    { 0x0794,   "The Coca-Cola Company" },
    { 0x0795,   "GASTEC CORPORATION" },
    { 0x0796,   "StarLeaf Ltd" },
    { 0x0797,   "Water-i.d. GmbH" },
    { 0x0798,   "HoloKit, Inc." },
    { 0x0799,   "PlantChoir Inc." },
    { 0x079A,   "GuangDong Oppo Mobile Telecommunications Corp., Ltd." },
    { 0x079B,   "CST ELECTRONICS (PROPRIETARY) LIMITED" },
    { 0x079C,   "Sky UK Limited" },
    { 0x079D,   "Digibale Pty Ltd" },
    { 0x079E,   "Smartloxx GmbH" },
    { 0x079F,   "Pune Scientific LLP" },
    { 0x07A0,   "Regent Beleuchtungskorper AG" },
    { 0x07A1,   "Apollo Neuroscience, Inc." },
    { 0x07A2,   "Roku, Inc." },
    { 0x07A3,   "Comcast Cable" },
    { 0x07A4,   "Xiamen Mage Information Technology Co., Ltd." },
    { 0x07A5,   "RAB Lighting, Inc." },
    { 0x07A6,   "Musen Connect, Inc." },
    { 0x07A7,   "Zume, Inc." },
    { 0x07A8,   "conbee GmbH" },
    { 0x07A9,   "Bruel & Kjaer Sound & Vibration" },
    { 0x07AA,   "The Kroger Co." },
    { 0x07AB,   "Granite River Solutions, Inc." },
    { 0x07AC,   "LoupeDeck Oy" },
    { 0x07AD,   "New H3C Technologies Co.,Ltd" },
    { 0x07AE,   "Aurea Solucoes Tecnologicas Ltda." },
    { 0x07AF,   "Hong Kong Bouffalo Lab Limited" },
    { 0x07B0,   "GV Concepts Inc." },
    { 0x07B1,   "Thomas Dynamics, LLC" },
    { 0x07B2,   "Moeco IOT Inc." },
    { 0x07B3,   "2N TELEKOMUNIKACE a.s." },
    { 0x07B4,   "Hormann KG Antriebstechnik" },
    { 0x07B5,   "CRONO CHIP, S.L." },
    { 0x07B6,   "Soundbrenner Limited" },
    { 0x07B7,   "ETABLISSEMENTS GEORGES RENAULT" },
    { 0x07B8,   "iSwip" },
    { 0x07B9,   "Epona Biotec Limited" },
    { 0x07BA,   "Battery-Biz Inc." },
    { 0x07BB,   "EPIC S.R.L." },
    { 0x07BC,   "KD CIRCUITS LLC" },
    { 0x07BD,   "Genedrive Diagnostics Ltd" },
    { 0x07BE,   "Axentia Technologies AB" },
    { 0x07BF,   "REGULA Ltd." },
    { 0x07C0,   "Biral AG" },
    { 0x07C1,   "A.W. Chesterton Company" },
    { 0x07C2,   "Radinn AB" },
    { 0x07C3,   "CIMTechniques, Inc." },
    { 0x07C4,   "Johnson Health Tech NA" },
    { 0x07C5,   "June Life, Inc." },
    { 0x07C6,   "Bluenetics GmbH" },
    { 0x07C7,   "iaconicDesign Inc." },
    { 0x07C8,   "WRLDS Creations AB" },
    { 0x07C9,   "Skullcandy, Inc." },
    { 0x07CA,   "Modul-System HH AB" },
    { 0x07CB,   "West Pharmaceutical Services, Inc." },
    { 0x07CC,   "Barnacle Systems Inc." },
    { 0x07CD,   "Smart Wave Technologies Canada Inc" },
    { 0x07CE,   "Shanghai Top-Chip Microelectronics Tech. Co., LTD" },
    { 0x07CF,   "NeoSensory, Inc." },
    { 0x07D0,   "Hangzhou Tuya Information  Technology Co., Ltd" },
    { 0x07D1,   "Shanghai Panchip Microelectronics Co., Ltd" },
    { 0x07D2,   "React Accessibility Limited" },
    { 0x07D3,   "LIVNEX Co.,Ltd." },
    { 0x07D4,   "Kano Computing Limited" },
    { 0x07D5,   "hoots classic GmbH" },
    { 0x07D6,   "ecobee Inc." },
    { 0x07D7,   "Nanjing Qinheng Microelectronics Co., Ltd" },
    { 0x07D8,   "SOLUTIONS AMBRA INC." },
    { 0x07D9,   "Micro-Design, Inc." },
    { 0x07DA,   "STARLITE Co., Ltd." },
    { 0x07DB,   "Remedee Labs" },
    { 0x07DC,   "ThingOS GmbH & Co KG" },
    { 0x07DD,   "Linear Circuits" },
    { 0x07DE,   "Unlimited Engineering SL" },
    { 0x07DF,   "Snap-on Incorporated" },
    { 0x07E0,   "Edifier International Limited" },
    { 0x07E1,   "Lucie Labs" },
    { 0x07E2,   "Alfred Kaercher SE & Co. KG" },
    { 0x07E3,   "Airoha Technology Corp." },
    { 0x07E4,   "Geeksme S.L." },
    { 0x07E5,   "Minut, Inc." },
    { 0x07E6,   "Waybeyond Limited" },
    { 0x07E7,   "Komfort IQ, Inc." },
    { 0x07E8,   "Packetcraft, Inc." },
    { 0x07E9,   "Häfele GmbH & Co KG" },
    { 0x07EA,   "ShapeLog, Inc." },
    { 0x07EB,   "NOVABASE S.R.L." },
    { 0x07EC,   "Frecce LLC" },
    { 0x07ED,   "Joule IQ, INC." },
    { 0x07EE,   "KidzTek LLC" },
    { 0x07EF,   "Aktiebolaget Sandvik Coromant" },
    { 0x07F0,   "e-moola.com Pty Ltd" },
    { 0x07F1,   "Zimi Innovations Pty Ltd" },
    { 0x07F2,   "SERENE GROUP, INC" },
    { 0x07F3,   "DIGISINE ENERGYTECH CO. LTD." },
    { 0x07F4,   "MEDIRLAB Orvosbiologiai Fejleszto Korlatolt Felelossegu Tarsasag" },
    { 0x07F5,   "Byton North America Corporation" },
    { 0x07F6,   "Shenzhen TonliScience and Technology Development Co.,Ltd" },
    { 0x07F7,   "Cesar Systems Ltd." },
    { 0x07F8,   "quip NYC Inc." },
    { 0x07F9,   "Direct Communication Solutions, Inc." },
    { 0x07FA,   "Klipsch Group, Inc." },
    { 0x07FB,   "Access Co., Ltd" },
    { 0x07FC,   "Renault SA" },
    { 0x07FD,   "JSK CO., LTD." },
    { 0x07FE,   "BIROTA" },
    { 0x07FF,   "maxon motor ltd." },
    { 0x0800,   "Optek" },
    { 0x0801,   "CRONUS ELECTRONICS LTD" },
    { 0x0802,   "NantSound, Inc." },
    { 0x0803,   "Domintell s.a." },
    { 0x0804,   "Andon Health Co.,Ltd" },
    { 0x0805,   "Urbanminded Ltd" },
    { 0x0806,   "TYRI Sweden AB" },
    { 0x0807,   "ECD Electronic Components GmbH Dresden" },
    { 0x0808,   "SISTEMAS KERN, SOCIEDAD ANÓMINA" },
    { 0x0809,   "Trulli Audio" },
    { 0x080A,   "Altaneos" },
    { 0x080B,   "Nanoleaf Canada Limited" },
    { 0x080C,   "Ingy B.V." },
    { 0x080D,   "Azbil Co." },
    { 0x080E,   "TATTCOM LLC" },
    { 0x080F,   "Paradox Engineering SA" },
    { 0x0810,   "LECO Corporation" },
    { 0x0811,   "Becker Antriebe GmbH" },
    { 0x0812,   "Mstream Technologies., Inc." },
    { 0x0813,   "Flextronics International USA Inc." },
    { 0x0814,   "Ossur hf." },
    { 0x0815,   "SKC Inc" },
    { 0x0816,   "SPICA SYSTEMS LLC" },
    { 0x0817,   "Wangs Alliance Corporation" },
    { 0x0818,   "tatwah SA" },
    { 0x0819,   "Hunter Douglas Inc" },
    { 0x081A,   "Shenzhen Conex" },
    { 0x081B,   "DIM3" },
    { 0x081C,   "Bobrick Washroom Equipment, Inc." },
    { 0x081D,   "Potrykus Holdings and Development LLC" },
    { 0x081E,   "iNFORM Technology GmbH" },
    { 0x081F,   "eSenseLab LTD" },
    { 0x0820,   "Brilliant Home Technology, Inc." },
    { 0x0821,   "INOVA Geophysical, Inc." },
    { 0x0822,   "adafruit industries" },
    { 0x0823,   "Nexite Ltd" },
    { 0x0824,   "8Power Limited" },
    { 0x0825,   "CME PTE. LTD." },
    { 0x0826,   "Hyundai Motor Company" },
    { 0x0827,   "Kickmaker" },
    { 0x0828,   "Shanghai Suisheng Information Technology Co., Ltd." },
    { 0x0829,   "HEXAGON METROLOGY DIVISION ROMER" },
    { 0x082A,   "Mitutoyo Corporation" },
    { 0x082B,   "shenzhen fitcare electronics Co.,Ltd" },
    { 0x082C,   "INGICS TECHNOLOGY CO., LTD." },
    { 0x082D,   "INCUS PERFORMANCE LTD." },
    { 0x082E,   "ABB S.p.A." },
    { 0x082F,   "Blippit AB" },
    { 0x0830,   "Core Health and Fitness LLC" },
    { 0x0831,   "Foxble, LLC" },
    { 0x0832,   "Intermotive,Inc." },
    { 0x0833,   "Conneqtech B.V." },
    { 0x0834,   "RIKEN KEIKI CO., LTD.," },
    { 0x0835,   "Canopy Growth Corporation" },
    { 0x0836,   "Bitwards Oy" },
    { 0x0837,   "vivo Mobile Communication Co., Ltd." },
    { 0x0838,   "Etymotic Research, Inc." },
    { 0x0839,   "A puissance 3" },
    { 0x083A,   "BPW Bergische Achsen Kommanditgesellschaft" },
    { 0x083B,   "Piaggio Fast Forward" },
    { 0x083C,   "BeerTech LTD" },
    { 0x083D,   "Tokenize, Inc." },
    { 0x083E,   "Zorachka LTD" },
    { 0x083F,   "D-Link Corp." },
    { 0x0840,   "Down Range Systems LLC" },
    { 0x0841,   "General Luminaire (Shanghai) Co., Ltd." },
    { 0x0842,   "Tangshan HongJia electronic technology co., LTD." },
    { 0x0843,   "FRAGRANCE DELIVERY TECHNOLOGIES LTD" },
    { 0x0844,   "Pepperl + Fuchs GmbH" },
    { 0x0845,   "Dometic Corporation" },
    { 0x0846,   "USound GmbH" },
    { 0x0847,   "DNANUDGE LIMITED" },
    { 0x0848,   "JUJU JOINTS CANADA CORP." },
    { 0x0849,   "Dopple Technologies B.V." },
    { 0x084A,   "ARCOM" },
    { 0x084B,   "Biotechware SRL" },
    { 0x084C,   "ORSO Inc." },
    { 0x084D,   "SafePort" },
    { 0x084E,   "Carol Cole Company" },
    { 0x084F,   "Embedded Fitness B.V." },
    { 0x0850,   "Yealink (Xiamen) Network Technology Co.,LTD" },
    { 0x0851,   "Subeca, Inc." },
    { 0x0852,   "Cognosos, Inc." },
    { 0x0853,   "Pektron Group Limited" },
    { 0x0854,   "Tap Sound System" },
    { 0x0855,   "Helios Sports, Inc." },
    { 0x0856,   "Canopy Growth Corporation" },
    { 0x0857,   "Parsyl Inc" },
    { 0x0858,   "SOUNDBOKS" },
    { 0x0859,   "BlueUp" },
    { 0x085A,   "DAKATECH" },
    { 0x085B,   "Nisshinbo Micro Devices Inc." },
    { 0x085C,   "ACOS CO.,LTD." },
    { 0x085D,   "Guilin Zhishen Information Technology Co.,Ltd." },
    { 0x085E,   "Krog Systems LLC" },
    { 0x085F,   "COMPEGPS TEAM,SOCIEDAD LIMITADA" },
    { 0x0860,   "Alflex Products B.V." },
    { 0x0861,   "SmartSensor Labs Ltd" },
    { 0x0862,   "SmartDrive" },
    { 0x0863,   "Yo-tronics Technology Co., Ltd." },
    { 0x0864,   "Rafaelmicro" },
    { 0x0865,   "Emergency Lighting Products Limited" },
    { 0x0866,   "LAONZ Co.,Ltd" },
    { 0x0867,   "Western Digital Techologies, Inc." },
    { 0x0868,   "WIOsense GmbH & Co. KG" },
    { 0x0869,   "EVVA Sicherheitstechnologie GmbH" },
    { 0x086A,   "Odic Incorporated" },
    { 0x086B,   "Pacific Track, LLC" },
    { 0x086C,   "Revvo Technologies, Inc." },
    { 0x086D,   "Biometrika d.o.o." },
    { 0x086E,   "Vorwerk Elektrowerke GmbH & Co. KG" },
    { 0x086F,   "Trackunit A/S" },
    { 0x0870,   "Wyze Labs, Inc" },
    { 0x0871,   "Dension Elektronikai Kft." },
    { 0x0872,   "11 Health & Technologies Limited" },
    { 0x0873,   "Innophase Incorporated" },
    { 0x0874,   "Treegreen Limited" },
    { 0x0875,   "Berner International LLC" },
    { 0x0876,   "SmartResQ ApS" },
    { 0x0877,   "Tome, Inc." },
    { 0x0878,   "The Chamberlain Group, Inc." },
    { 0x0879,   "MIZUNO Corporation" },
    { 0x087A,   "ZRF, LLC" },
    { 0x087B,   "BYSTAMP" },
    { 0x087C,   "Crosscan GmbH" },
    { 0x087D,   "Konftel AB" },
    { 0x087E,   "1bar.net Limited" },
    { 0x087F,   "Phillips Connect Technologies LLC" },
    { 0x0880,   "imagiLabs AB" },
    { 0x0881,   "Optalert" },
    { 0x0882,   "PSYONIC, Inc." },
    { 0x0883,   "Wintersteiger AG" },
    { 0x0884,   "Controlid Industria, Comercio de Hardware e Servicos de Tecnologia Ltda" },
    { 0x0885,   "LEVOLOR INC" },
    { 0x0886,   "Movella Technologies B.V." },
    { 0x0887,   "Hydro-Gear Limited Partnership" },
    { 0x0888,   "EnPointe Fencing Pty Ltd" },
    { 0x0889,   "XANTHIO" },
    { 0x088A,   "sclak s.r.l." },
    { 0x088B,   "Tricorder Arraay Technologies LLC" },
    { 0x088C,   "GB Solution co.,Ltd" },
    { 0x088D,   "Soliton Systems K.K." },
    { 0x088E,   "GIGA-TMS INC" },
    { 0x088F,   "Tait International Limited" },
    { 0x0890,   "NICHIEI INTEC CO., LTD." },
    { 0x0891,   "SmartWireless GmbH & Co. KG" },
    { 0x0892,   "Ingenieurbuero Birnfeld UG (haftungsbeschraenkt)" },
    { 0x0893,   "Maytronics Ltd" },
    { 0x0894,   "EPIFIT" },
    { 0x0895,   "Gimer medical" },
    { 0x0896,   "Nokian Renkaat Oyj" },
    { 0x0897,   "Current Lighting Solutions LLC" },
    { 0x0898,   "Sensibo, Inc." },
    { 0x0899,   "SFS unimarket AG" },
    { 0x089A,   "Private limited company \"Teltonika\"" },
    { 0x089B,   "Saucon Technologies" },
    { 0x089C,   "Embedded Devices Co. Company" },
    { 0x089D,   "J-J.A.D.E. Enterprise LLC" },
    { 0x089E,   "i-SENS, inc." },
    { 0x089F,   "Witschi Electronic Ltd" },
    { 0x08A0,   "Aclara Technologies LLC" },
    { 0x08A1,   "EXEO TECH CORPORATION" },
    { 0x08A2,   "Epic Systems Co., Ltd." },
    { 0x08A3,   "Hoffmann SE" },
    { 0x08A4,   "Realme Chongqing Mobile Telecommunications Corp., Ltd." },
    { 0x08A5,   "UMEHEAL Ltd" },
    { 0x08A6,   "Intelligenceworks Inc." },
    { 0x08A7,   "TGR 1.618 Limited" },
    { 0x08A8,   "Shanghai Kfcube Inc" },
    { 0x08A9,   "Fraunhofer IIS" },
    { 0x08AA,   "SZ DJI TECHNOLOGY CO.,LTD" },
    { 0x08AB,   "Coburn Technology, LLC" },
    { 0x08AC,   "Topre Corporation" },
    { 0x08AD,   "Kayamatics Limited" },
    { 0x08AE,   "Moticon ReGo AG" },
    { 0x08AF,   "Polidea Sp. z o.o." },
    { 0x08B0,   "Trivedi Advanced Technologies LLC" },
    { 0x08B1,   "CORE|vision BV" },
    { 0x08B2,   "PF SCHWEISSTECHNOLOGIE GMBH" },
    { 0x08B3,   "IONIQ Skincare GmbH & Co. KG" },
    { 0x08B4,   "Sengled Co., Ltd." },
    { 0x08B5,   "TransferFi" },
    { 0x08B6,   "Boehringer Ingelheim Vetmedica GmbH" },
    { 0x08B7,   "ABB Inc" },
    { 0x08B8,   "Check Technology Solutions LLC" },
    { 0x08B9,   "U-Shin Ltd." },
    { 0x08BA,   "HYPER ICE, INC." },
    { 0x08BB,   "Tokai-rika co.,ltd." },
    { 0x08BC,   "Prevayl Limited" },
    { 0x08BD,   "bf1systems limited" },
    { 0x08BE,   "ubisys technologies GmbH" },
    { 0x08BF,   "SIRC Co., Ltd." },
    { 0x08C0,   "Accent Advanced Systems SLU" },
    { 0x08C1,   "Rayden.Earth LTD" },
    { 0x08C2,   "Lindinvent AB" },
    { 0x08C3,   "CHIPOLO d.o.o." },
    { 0x08C4,   "CellAssist, LLC" },
    { 0x08C5,   "J. Wagner GmbH" },
    { 0x08C6,   "Integra Optics Inc" },
    { 0x08C7,   "Monadnock Systems Ltd." },
    { 0x08C8,   "Liteboxer Technologies Inc." },
    { 0x08C9,   "Noventa AG" },
    { 0x08CA,   "Nubia Technology Co.,Ltd." },
    { 0x08CB,   "JT INNOVATIONS LIMITED" },
    { 0x08CC,   "TGM TECHNOLOGY CO., LTD." },
    { 0x08CD,   "ifly" },
    { 0x08CE,   "ZIMI CORPORATION" },
    { 0x08CF,   "betternotstealmybike UG (with limited liability)" },
    { 0x08D0,   "ESTOM Infotech Kft." },
    { 0x08D1,   "Sensovium Inc." },
    { 0x08D2,   "Virscient Limited" },
    { 0x08D3,   "Novel Bits, LLC" },
    { 0x08D4,   "ADATA Technology Co., LTD." },
    { 0x08D5,   "KEYes" },
    { 0x08D6,   "Nome Oy" },
    { 0x08D7,   "Inovonics Corp" },
    { 0x08D8,   "WARES" },
    { 0x08D9,   "Pointr Labs Limited" },
    { 0x08DA,   "Miridia Technology Incorporated" },
    { 0x08DB,   "Tertium Technology" },
    { 0x08DC,   "SHENZHEN AUKEY E BUSINESS CO., LTD" },
    { 0x08DD,   "code-Q" },
    { 0x08DE,   "TE Connectivity Corporation" },
    { 0x08DF,   "IRIS OHYAMA CO.,LTD." },
    { 0x08E0,   "Philia Technology" },
    { 0x08E1,   "KOZO KEIKAKU ENGINEERING Inc." },
    { 0x08E2,   "Shenzhen Simo Technology co. LTD" },
    { 0x08E3,   "Republic Wireless, Inc." },
    { 0x08E4,   "Rashidov ltd" },
    { 0x08E5,   "Crowd Connected Ltd" },
    { 0x08E6,   "Eneso Tecnologia de Adaptacion S.L." },
    { 0x08E7,   "Barrot Technology Co.,Ltd." },
    { 0x08E8,   "Naonext" },
    { 0x08E9,   "Taiwan Intelligent Home Corp." },
    { 0x08EA,   "COWBELL ENGINEERING CO.,LTD." },
    { 0x08EB,   "Beijing Big Moment Technology Co., Ltd." },
    { 0x08EC,   "Denso Corporation" },
    { 0x08ED,   "IMI Hydronic Engineering International SA" },
    { 0x08EE,   "Askey Computer Corp." },
    { 0x08EF,   "Cumulus Digital Systems, Inc" },
    { 0x08F0,   "Joovv, Inc." },
    { 0x08F1,   "The L.S. Starrett Company" },
    { 0x08F2,   "Microoled" },
    { 0x08F3,   "PSP - Pauli Services & Products GmbH" },
    { 0x08F4,   "Kodimo Technologies Company Limited" },
    { 0x08F5,   "Tymtix Technologies Private Limited" },
    { 0x08F6,   "Dermal Photonics Corporation" },
    { 0x08F7,   "MTD Products Inc & Affiliates" },
    { 0x08F8,   "instagrid GmbH" },
    { 0x08F9,   "Spacelabs Medical Inc." },
    { 0x08FA,   "Troo Corporation" },
    { 0x08FB,   "Darkglass Electronics Oy" },
    { 0x08FC,   "Hill-Rom" },
    { 0x08FD,   "BioIntelliSense, Inc." },
    { 0x08FE,   "Ketronixs Sdn Bhd" },
    { 0x08FF,   "Plastimold Products, Inc" },
    { 0x0900,   "Beijing Zizai Technology Co., LTD." },
    { 0x0901,   "Lucimed" },
    { 0x0902,   "TSC Auto-ID Technology Co., Ltd." },
    { 0x0903,   "DATAMARS, Inc." },
    { 0x0904,   "SUNCORPORATION" },
    { 0x0905,   "Yandex Services AG" },
    { 0x0906,   "Scope Logistical Solutions" },
    { 0x0907,   "User Hello, LLC" },
    { 0x0908,   "Pinpoint Innovations Limited" },
    { 0x0909,   "70mai Co.,Ltd." },
    { 0x090A,   "Zhuhai Hoksi Technology CO.,LTD" },
    { 0x090B,   "EMBR labs, INC" },
    { 0x090C,   "Radiawave Technologies Co.,Ltd." },
    { 0x090D,   "IOT Invent GmbH" },
    { 0x090E,   "OPTIMUSIOT TECH LLP" },
    { 0x090F,   "VC Inc." },
    { 0x0910,   "ASR Microelectronics (Shanghai) Co., Ltd." },
    { 0x0911,   "Douglas Lighting Controls Inc." },
    { 0x0912,   "Nerbio Medical Software Platforms Inc" },
    { 0x0913,   "Braveheart Wireless, Inc." },
    { 0x0914,   "INEO-SENSE" },
    { 0x0915,   "Honda Motor Co., Ltd." },
    { 0x0916,   "Ambient Sensors LLC" },
    { 0x0917,   "ASR Microelectronics(ShenZhen)Co., Ltd." },
    { 0x0918,   "Technosphere Labs Pvt. Ltd." },
    { 0x0919,   "NO SMD LIMITED" },
    { 0x091A,   "Albertronic BV" },
    { 0x091B,   "Luminostics, Inc." },
    { 0x091C,   "Oblamatik AG" },
    { 0x091D,   "Innokind, Inc." },
    { 0x091E,   "Melbot Studios, Sociedad Limitada" },
    { 0x091F,   "Myzee Technology" },
    { 0x0920,   "Omnisense Limited" },
    { 0x0921,   "KAHA PTE. LTD." },
    { 0x0922,   "Shanghai MXCHIP Information Technology Co., Ltd." },
    { 0x0923,   "JSB TECH PTE LTD" },
    { 0x0924,   "Fundacion Tecnalia Research and Innovation" },
    { 0x0925,   "Yukai Engineering Inc." },
    { 0x0926,   "Gooligum Technologies Pty Ltd" },
    { 0x0927,   "ROOQ GmbH" },
    { 0x0928,   "AiRISTA" },
    { 0x0929,   "Qingdao Haier Technology Co., Ltd." },
    { 0x092A,   "Sappl Verwaltungs- und Betriebs GmbH" },
    { 0x092B,   "TekHome" },
    { 0x092C,   "PCI Private Limited" },
    { 0x092D,   "Leggett & Platt, Incorporated" },
    { 0x092E,   "PS GmbH" },
    { 0x092F,   "C.O.B.O. SpA" },
    { 0x0930,   "James Walker RotaBolt Limited" },
    { 0x0931,   "BREATHINGS Co., Ltd." },
    { 0x0932,   "BarVision, LLC" },
    { 0x0933,   "SRAM" },
    { 0x0934,   "KiteSpring Inc." },
    { 0x0935,   "Reconnect, Inc." },
    { 0x0936,   "Elekon AG" },
    { 0x0937,   "RealThingks GmbH" },
    { 0x0938,   "Henway Technologies, LTD." },
    { 0x0939,   "ASTEM Co.,Ltd." },
    { 0x093A,   "LinkedSemi Microelectronics (Xiamen) Co., Ltd" },
    { 0x093B,   "ENSESO LLC" },
    { 0x093C,   "Xenoma Inc." },
    { 0x093D,   "Adolf Wuerth GmbH & Co KG" },
    { 0x093E,   "Catalyft Labs, Inc." },
    { 0x093F,   "JEPICO Corporation" },
    { 0x0940,   "Hero Workout GmbH" },
    { 0x0941,   "Rivian Automotive, LLC" },
    { 0x0942,   "TRANSSION HOLDINGS LIMITED" },
    { 0x0943,   "Inovonics Corp." },
    { 0x0944,   "Agitron d.o.o." },
    { 0x0945,   "Globe (Jiangsu) Co., Ltd" },
    { 0x0946,   "AMC International Alfa Metalcraft Corporation AG" },
    { 0x0947,   "First Light Technologies Ltd." },
    { 0x0948,   "Wearable Link Limited" },
    { 0x0949,   "Metronom Health Europe" },
    { 0x094A,   "Zwift, Inc." },
    { 0x094B,   "Kindeva Drug Delivery L.P." },
    { 0x094C,   "GimmiSys GmbH" },
    { 0x094D,   "tkLABS INC." },
    { 0x094E,   "PassiveBolt, Inc." },
    { 0x094F,   "Limited Liability Company \"Mikrotikls\"" },
    { 0x0950,   "Capetech" },
    { 0x0951,   "PPRS" },
    { 0x0952,   "Apptricity Corporation" },
    { 0x0953,   "LogiLube, LLC" },
    { 0x0954,   "Julbo" },
    { 0x0955,   "Breville Group" },
    { 0x0956,   "Kerlink" },
    { 0x0957,   "Ohsung Electronics" },
    { 0x0958,   "ZTE Corporation" },
    { 0x0959,   "HerdDogg, Inc" },
    { 0x095A,   "Selekt Bilgisayar, lletisim Urunleri lnsaat Sanayi ve Ticaret Limited Sirketi" },
    { 0x095B,   "Lismore Instruments Limited" },
    { 0x095C,   "LogiLube, LLC" },
    { 0x095D,   "Electronic Theatre Controls" },
    { 0x095E,   "BioEchoNet inc." },
    { 0x095F,   "NUANCE HEARING LTD" },
    { 0x0960,   "Sena Technologies Inc." },
    { 0x0961,   "Linkura AB" },
    { 0x0962,   "GL Solutions K.K." },
    { 0x0963,   "Moonbird BV" },
    { 0x0964,   "Countrymate Technology Limited" },
    { 0x0965,   "Asahi Kasei Corporation" },
    { 0x0966,   "PointGuard, LLC" },
    { 0x0967,   "Neo Materials and Consulting Inc." },
    { 0x0968,   "Actev Motors, Inc." },
    { 0x0969,   "Woan Technology (Shenzhen) Co., Ltd." },
    { 0x096A,   "dricos, Inc." },
    { 0x096B,   "Guide ID B.V." },
    { 0x096C,   "9374-7319 Quebec inc" },
    { 0x096D,   "Gunwerks, LLC" },
    { 0x096E,   "Band Industries, inc." },
    { 0x096F,   "Lund Motion Products, Inc." },
    { 0x0970,   "IBA Dosimetry GmbH" },
    { 0x0971,   "GA" },
    { 0x0972,   "Closed Joint Stock Company \"Zavod Flometr\" (\"Zavod Flometr\" CJSC)" },
    { 0x0973,   "Popit Oy" },
    { 0x0974,   "ABEYE" },
    { 0x0975,   "BlueIOT(Beijing) Technology Co.,Ltd" },
    { 0x0976,   "Fauna Audio GmbH" },
    { 0x0977,   "TOYOTA motor corporation" },
    { 0x0978,   "ZifferEins GmbH & Co. KG" },
    { 0x0979,   "BIOTRONIK SE & Co. KG" },
    { 0x097A,   "CORE CORPORATION" },
    { 0x097B,   "CTEK Sweden AB" },
    { 0x097C,   "Thorley Industries, LLC" },
    { 0x097D,   "CLB B.V." },
    { 0x097E,   "SonicSensory Inc" },
    { 0x097F,   "ISEMAR S.R.L." },
    { 0x0980,   "DEKRA TESTING AND CERTIFICATION, S.A.U." },
    { 0x0981,   "Bernard Krone Holding SE & Co.KG" },
    { 0x0982,   "ELPRO-BUCHS AG" },
    { 0x0983,   "Feedback Sports LLC" },
    { 0x0984,   "TeraTron GmbH" },
    { 0x0985,   "Lumos Health Inc." },
    { 0x0986,   "Cello Hill, LLC" },
    { 0x0987,   "TSE BRAKES, INC." },
    { 0x0988,   "BHM-Tech Produktionsgesellschaft m.b.H" },
    { 0x0989,   "WIKA Alexander Wiegand SE & Co.KG" },
    { 0x098A,   "Biovigil" },
    { 0x098B,   "Mequonic Engineering, S.L." },
    { 0x098C,   "bGrid B.V." },
    { 0x098D,   "C3-WIRELESS, LLC" },
    { 0x098E,   "ADVEEZ" },
    { 0x098F,   "Aktiebolaget Regin" },
    { 0x0990,   "Anton Paar GmbH" },
    { 0x0991,   "Telenor ASA" },
    { 0x0992,   "Big Kaiser Precision Tooling Ltd" },
    { 0x0993,   "Absolute Audio Labs B.V." },
    { 0x0994,   "VT42 Pty Ltd" },
    { 0x0995,   "Bronkhorst High-Tech B.V." },
    { 0x0996,   "C. & E. Fein GmbH" },
    { 0x0997,   "NextMind" },
    { 0x0998,   "Pixie Dust Technologies, Inc." },
    { 0x0999,   "eTactica ehf" },
    { 0x099A,   "New Audio LLC" },
    { 0x099B,   "Sendum Wireless Corporation" },
    { 0x099C,   "deister electronic GmbH" },
    { 0x099D,   "YKK AP Inc." },
    { 0x099E,   "Step One Limited" },
    { 0x099F,   "Koya Medical, Inc." },
    { 0x09A0,   "Proof Diagnostics, Inc." },
    { 0x09A1,   "VOS Systems, LLC" },
    { 0x09A2,   "ENGAGENOW DATA SCIENCES PRIVATE LIMITED" },
    { 0x09A3,   "ARDUINO SA" },
    { 0x09A4,   "KUMHO ELECTRICS, INC" },
    { 0x09A5,   "Security Enhancement Systems, LLC" },
    { 0x09A6,   "BEIJING ELECTRIC VEHICLE CO.,LTD" },
    { 0x09A7,   "Paybuddy ApS" },
    { 0x09A8,   "KHN Solutions LLC" },
    { 0x09A9,   "Nippon Ceramic Co.,Ltd." },
    { 0x09AA,   "PHOTODYNAMIC INCORPORATED" },
    { 0x09AB,   "DashLogic, Inc." },
    { 0x09AC,   "Ambiq" },
    { 0x09AD,   "Narhwall Inc." },
    { 0x09AE,   "Pozyx NV" },
    { 0x09AF,   "ifLink Open Community" },
    { 0x09B0,   "Deublin Company, LLC" },
    { 0x09B1,   "BLINQY" },
    { 0x09B2,   "DYPHI" },
    { 0x09B3,   "BlueX Microelectronics Corp Ltd." },
    { 0x09B4,   "PentaLock Aps." },
    { 0x09B5,   "AUTEC Gesellschaft fuer Automationstechnik mbH" },
    { 0x09B6,   "Pegasus Technologies, Inc." },
    { 0x09B7,   "Bout Labs, LLC" },
    { 0x09B8,   "PlayerData Limited" },
    { 0x09B9,   "SAVOY ELECTRONIC LIGHTING" },
    { 0x09BA,   "Elimo Engineering Ltd" },
    { 0x09BB,   "SkyStream Corporation" },
    { 0x09BC,   "Aerosens LLC" },
    { 0x09BD,   "Centre Suisse d'Electronique et de Microtechnique SA" },
    { 0x09BE,   "Vessel Ltd." },
    { 0x09BF,   "Span.IO, Inc." },
    { 0x09C0,   "AnotherBrain inc." },
    { 0x09C1,   "Rosewill" },
    { 0x09C2,   "Universal Audio, Inc." },
    { 0x09C3,   "JAPAN TOBACCO INC." },
    { 0x09C4,   "UVISIO" },
    { 0x09C5,   "HungYi Microelectronics Co.,Ltd." },
    { 0x09C6,   "Honor Device Co., Ltd." },
    { 0x09C7,   "Combustion, LLC" },
    { 0x09C8,   "XUNTONG" },
    { 0x09C9,   "CrowdGlow Ltd" },
    { 0x09CA,   "Mobitrace" },
    { 0x09CB,   "Hx Engineering, LLC" },
    { 0x09CC,   "Senso4s d.o.o." },
    { 0x09CD,   "Blyott" },
    { 0x09CE,   "Julius Blum GmbH" },
    { 0x09CF,   "BlueStreak IoT, LLC" },
    { 0x09D0,   "Chess Wise B.V." },
    { 0x09D1,   "ABLEPAY TECHNOLOGIES AS" },
    { 0x09D2,   "Temperature Sensitive Solutions Systems Sweden AB" },
    { 0x09D3,   "HeartHero, inc." },
    { 0x09D4,   "ORBIS Inc." },
    { 0x09D5,   "GEAR RADIO ELECTRONICS CORP." },
    { 0x09D6,   "EAR TEKNIK ISITME VE ODIOMETRI CIHAZLARI SANAYI VE TICARET ANONIM SIRKETI" },
    { 0x09D7,   "Coyotta" },
    { 0x09D8,   "Synergy Tecnologia em Sistemas Ltda" },
    { 0x09D9,   "VivoSensMedical GmbH" },
    { 0x09DA,   "Nagravision SA" },
    { 0x09DB,   "Bionic Avionics Inc." },
    { 0x09DC,   "AON2 Ltd." },
    { 0x09DD,   "Innoware Development AB" },
    { 0x09DE,   "JLD Technology Solutions, LLC" },
    { 0x09DF,   "Magnus Technology Sdn Bhd" },
    { 0x09E0,   "Preddio Technologies Inc." },
    { 0x09E1,   "Tag-N-Trac Inc" },
    { 0x09E2,   "Wuhan Linptech Co.,Ltd." },
    { 0x09E3,   "Friday Home Aps" },
    { 0x09E4,   "CPS AS" },
    { 0x09E5,   "Mobilogix" },
    { 0x09E6,   "Masonite Corporation" },
    { 0x09E7,   "Kabushikigaisha HANERON" },
    { 0x09E8,   "Melange Systems Pvt. Ltd." },
    { 0x09E9,   "LumenRadio AB" },
    { 0x09EA,   "Athlos Oy" },
    { 0x09EB,   "KEAN ELECTRONICS PTY LTD" },
    { 0x09EC,   "Yukon advanced optics worldwide, UAB" },
    { 0x09ED,   "Sibel Inc." },
    { 0x09EE,   "OJMAR SA" },
    { 0x09EF,   "Steinel Solutions AG" },
    { 0x09F0,   "WatchGas B.V." },
    { 0x09F1,   "OM Digital Solutions Corporation" },
    { 0x09F2,   "Audeara Pty Ltd" },
    { 0x09F3,   "Beijing Zero Zero Infinity Technology Co.,Ltd." },
    { 0x09F4,   "Spectrum Technologies, Inc." },
    { 0x09F5,   "OKI Electric Industry Co., Ltd" },
    { 0x09F6,   "Mobile Action Technology Inc." },
    { 0x09F7,   "SENSATEC Co., Ltd." },
    { 0x09F8,   "R.O. S.R.L." },
    { 0x09F9,   "Hangzhou Yaguan Technology Co. LTD" },
    { 0x09FA,   "Listen Technologies Corporation" },
    { 0x09FB,   "TOITU CO., LTD." },
    { 0x09FC,   "Confidex" },
    { 0x09FD,   "Keep Technologies, Inc." },
    { 0x09FE,   "Lichtvision Engineering GmbH" },
    { 0x09FF,   "AIRSTAR" },
    { 0x0A00,   "Ampler Bikes OU" },
    { 0x0A01,   "Cleveron AS" },
    { 0x0A02,   "Ayxon-Dynamics GmbH" },
    { 0x0A03,   "donutrobotics Co., Ltd." },
    { 0x0A04,   "Flosonics Medical" },
    { 0x0A05,   "Southwire Company, LLC" },
    { 0x0A06,   "Shanghai wuqi microelectronics Co.,Ltd" },
    { 0x0A07,   "Reflow Pty Ltd" },
    { 0x0A08,   "Oras Oy" },
    { 0x0A09,   "ECCT" },
    { 0x0A0A,   "Volan Technology Inc." },
    { 0x0A0B,   "SIANA Systems" },
    { 0x0A0C,   "Shanghai Yidian Intelligent Technology Co., Ltd." },
    { 0x0A0D,   "Blue Peacock GmbH" },
    { 0x0A0E,   "Roland Corporation" },
    { 0x0A0F,   "LIXIL Corporation" },
    { 0x0A10,   "SUBARU Corporation" },
    { 0x0A11,   "Sensolus" },
    { 0x0A12,   "Dyson Technology Limited" },
    { 0x0A13,   "Tec4med LifeScience GmbH" },
    { 0x0A14,   "CROXEL, INC." },
    { 0x0A15,   "Syng Inc" },
    { 0x0A16,   "RIDE VISION LTD" },
    { 0x0A17,   "Plume Design Inc" },
    { 0x0A18,   "Cambridge Animal Technologies Ltd" },
    { 0x0A19,   "Maxell, Ltd." },
    { 0x0A1A,   "Link Labs, Inc." },
    { 0x0A1B,   "Embrava Pty Ltd" },
    { 0x0A1C,   "INPEAK S.C." },
    { 0x0A1D,   "API-K" },
    { 0x0A1E,   "CombiQ AB" },
    { 0x0A1F,   "DeVilbiss Healthcare LLC" },
    { 0x0A20,   "Jiangxi Innotech Technology Co., Ltd" },
    { 0x0A21,   "Apollogic Sp. z o.o." },
    { 0x0A22,   "DAIICHIKOSHO CO., LTD." },
    { 0x0A23,   "BIXOLON CO.,LTD" },
    { 0x0A24,   "Atmosic Technologies, Inc." },
    { 0x0A25,   "Eran Financial Services LLC" },
    { 0x0A26,   "Louis Vuitton" },
    { 0x0A27,   "AYU DEVICES PRIVATE LIMITED" },
    { 0x0A28,   "NanoFlex Power Corporation" },
    { 0x0A29,   "Worthcloud Technology Co.,Ltd" },
    { 0x0A2A,   "Yamaha Corporation" },
    { 0x0A2B,   "PaceBait IVS" },
    { 0x0A2C,   "Shenzhen H&T Intelligent Control Co., Ltd" },
    { 0x0A2D,   "Shenzhen Feasycom Technology Co., Ltd." },
    { 0x0A2E,   "Zuma Array Limited" },
    { 0x0A2F,   "Instamic, Inc." },
    { 0x0A30,   "Air-Weigh" },
    { 0x0A31,   "Nevro Corp." },
    { 0x0A32,   "Pinnacle Technology, Inc." },
    { 0x0A33,   "WMF AG" },
    { 0x0A34,   "Luxer Corporation" },
    { 0x0A35,   "safectory GmbH" },
    { 0x0A36,   "NGK SPARK PLUG CO., LTD." },
    { 0x0A37,   "2587702 Ontario Inc." },
    { 0x0A38,   "Bouffalo Lab (Nanjing)., Ltd." },
    { 0x0A39,   "BLUETICKETING SRL" },
    { 0x0A3A,   "Incotex Co. Ltd." },
    { 0x0A3B,   "Galileo Technology Limited" },
    { 0x0A3C,   "Siteco GmbH" },
    { 0x0A3D,   "DELABIE" },
    { 0x0A3E,   "Hefei Yunlian Semiconductor Co., Ltd" },
    { 0x0A3F,   "Shenzhen Yopeak Optoelectronics Technology Co., Ltd." },
    { 0x0A40,   "GEWISS S.p.A." },
    { 0x0A41,   "OPEX Corporation" },
    { 0x0A42,   "Motionalysis, Inc." },
    { 0x0A43,   "Busch Systems International Inc." },
    { 0x0A44,   "Novidan, Inc." },
    { 0x0A45,   "3SI Security Systems, Inc" },
    { 0x0A46,   "Beijing HC-Infinite Technology Limited" },
    { 0x0A47,   "The Wand Company Ltd" },
    { 0x0A48,   "JRC Mobility Inc." },
    { 0x0A49,   "Venture Research Inc." },
    { 0x0A4A,   "Map Large, Inc." },
    { 0x0A4B,   "MistyWest Energy and Transport Ltd." },
    { 0x0A4C,   "SiFli Technologies (shanghai) Inc." },
    { 0x0A4D,   "Lockn Technologies Private Limited" },
    { 0x0A4E,   "Toytec Corporation" },
    { 0x0A4F,   "VANMOOF Global Holding B.V." },
    { 0x0A50,   "Nextscape Inc." },
    { 0x0A51,   "CSIRO" },
    { 0x0A52,   "Follow Sense Europe B.V." },
    { 0x0A53,   "KKM COMPANY LIMITED" },
    { 0x0A54,   "SQL Technologies Corp." },
    { 0x0A55,   "Inugo Systems Limited" },
    { 0x0A56,   "ambie" },
    { 0x0A57,   "Meizhou Guo Wei Electronics Co., Ltd" },
    { 0x0A58,   "Indigo Diabetes" },
    { 0x0A59,   "TourBuilt, LLC" },
    { 0x0A5A,   "Sontheim Industrie Elektronik GmbH" },
    { 0x0A5B,   "LEGIC Identsystems AG" },
    { 0x0A5C,   "Innovative Design Labs Inc." },
    { 0x0A5D,   "MG Energy Systems B.V." },
    { 0x0A5E,   "LaceClips llc" },
    { 0x0A5F,   "stryker" },
    { 0x0A60,   "DATANG SEMICONDUCTOR TECHNOLOGY CO.,LTD" },
    { 0x0A61,   "Smart Parks B.V." },
    { 0x0A62,   "MOKO TECHNOLOGY Ltd" },
    { 0x0A63,   "Gremsy JSC" },
    { 0x0A64,   "Geopal system A/S" },
    { 0x0A65,   "Lytx, INC." },
    { 0x0A66,   "JUSTMORPH PTE. LTD." },
    { 0x0A67,   "Beijing SuperHexa Century Technology CO. Ltd" },
    { 0x0A68,   "Focus Ingenieria SRL" },
    { 0x0A69,   "HAPPIEST BABY, INC." },
    { 0x0A6A,   "Scribble Design Inc." },
    { 0x0A6B,   "Olympic Ophthalmics, Inc." },
    { 0x0A6C,   "Pokkels" },
    { 0x0A6D,   "KUUKANJYOKIN Co.,Ltd." },
    { 0x0A6E,   "Pac Sane Limited" },
    { 0x0A6F,   "Warner Bros." },
    { 0x0A70,   "Ooma" },
    { 0x0A71,   "Senquip Pty Ltd" },
    { 0x0A72,   "Jumo GmbH & Co. KG" },
    { 0x0A73,   "Innohome Oy" },
    { 0x0A74,   "MICROSON S.A." },
    { 0x0A75,   "Delta Cycle Corporation" },
    { 0x0A76,   "Synaptics Incorporated" },
    { 0x0A77,   "AXTRO PTE. LTD." },
    { 0x0A78,   "Shenzhen Sunricher Technology Limited" },
    { 0x0A79,   "Webasto SE" },
    { 0x0A7A,   "Emlid Limited" },
    { 0x0A7B,   "UniqAir Oy" },
    { 0x0A7C,   "WAFERLOCK" },
    { 0x0A7D,   "Freedman Electronics Pty Ltd" },
    { 0x0A7E,   "KEBA Handover Automation GmbH" },
    { 0x0A7F,   "Intuity Medical" },
    { 0x0A80,   "Cleer Limited" },
    { 0x0A81,   "Universal Biosensors Pty Ltd" },
    { 0x0A82,   "Corsair" },
    { 0x0A83,   "Rivata, Inc." },
    { 0x0A84,   "Greennote Inc," },
    { 0x0A85,   "Snowball Technology Co., Ltd." },
    { 0x0A86,   "ALIZENT International" },
    { 0x0A87,   "Shanghai Smart System Technology Co., Ltd" },
    { 0x0A88,   "PSA Peugeot Citroen" },
    { 0x0A89,   "SES-Imagotag" },
    { 0x0A8A,   "HAINBUCH GMBH SPANNENDE TECHNIK" },
    { 0x0A8B,   "SANlight GmbH" },
    { 0x0A8C,   "DelpSys, s.r.o." },
    { 0x0A8D,   "JCM TECHNOLOGIES S.A." },
    { 0x0A8E,   "Perfect Company" },
    { 0x0A8F,   "TOTO LTD." },
    { 0x0A90,   "Shenzhen Grandsun Electronic Co.,Ltd." },
    { 0x0A91,   "Monarch International Inc." },
    { 0x0A92,   "Carestream Dental LLC" },
    { 0x0A93,   "GiPStech S.r.l." },
    { 0x0A94,   "OOBIK Inc." },
    { 0x0A95,   "Pamex Inc." },
    { 0x0A96,   "Lightricity Ltd" },
    { 0x0A97,   "SensTek" },
    { 0x0A98,   "Foil, Inc." },
    { 0x0A99,   "Shanghai high-flying electronics technology Co.,Ltd" },
    { 0x0A9A,   "TEMKIN ASSOCIATES, LLC" },
    { 0x0A9B,   "Eello LLC" },
    { 0x0A9C,   "Xi'an Fengyu Information Technology Co., Ltd." },
    { 0x0A9D,   "Canon Finetech Nisca Inc." },
    { 0x0A9E,   "LifePlus, Inc." },
    { 0x0A9F,   "ista International GmbH" },
    { 0x0AA0,   "Loy Tec electronics GmbH" },
    { 0x0AA1,   "LINCOGN TECHNOLOGY CO. LIMITED" },
    { 0x0AA2,   "Care Bloom, LLC" },
    { 0x0AA3,   "DIC Corporation" },
    { 0x0AA4,   "FAZEPRO LLC" },
    { 0x0AA5,   "Shenzhen Uascent Technology Co., Ltd" },
    { 0x0AA6,   "Realityworks, inc." },
    { 0x0AA7,   "Urbanista AB" },
    { 0x0AA8,   "Zencontrol Pty Ltd" },
    { 0x0AA9,   "Spintly, Inc." },
    { 0x0AAA,   "Computime International Ltd" },
    { 0x0AAB,   "Anhui Listenai Co" },
    { 0x0AAC,   "OSM HK Limited" },
    { 0x0AAD,   "Adevo Consulting AB" },
    { 0x0AAE,   "PS Engineering, Inc." },
    { 0x0AAF,   "AIAIAI ApS" },
    { 0x0AB0,   "Visiontronic s.r.o." },
    { 0x0AB1,   "InVue Security Products Inc" },
    { 0x0AB2,   "TouchTronics, Inc." },
    { 0x0AB3,   "INNER RANGE PTY. LTD." },
    { 0x0AB4,   "Ellenby Technologies, Inc." },
    { 0x0AB5,   "Elstat Electronics Ltd." },
    { 0x0AB6,   "Xenter, Inc." },
    { 0x0AB7,   "LogTag North America Inc." },
    { 0x0AB8,   "Sens.ai Incorporated" },
    { 0x0AB9,   "STL" },
    { 0x0ABA,   "Open Bionics Ltd." },
    { 0x0ABB,   "R-DAS, s.r.o." },
    { 0x0ABC,   "KCCS Mobile Engineering Co., Ltd." },
    { 0x0ABD,   "Inventas AS" },
    { 0x0ABE,   "Robkoo Information & Technologies Co., Ltd." },
    { 0x0ABF,   "PAUL HARTMANN AG" },
    { 0x0AC0,   "Omni-ID USA, INC." },
    { 0x0AC1,   "Shenzhen Jingxun Technology Co., Ltd." },
    { 0x0AC2,   "RealMega Microelectronics technology (Shanghai) Co. Ltd." },
    { 0x0AC3,   "Kenzen, Inc." },
    { 0x0AC4,   "CODIUM" },
    { 0x0AC5,   "Flexoptix GmbH" },
    { 0x0AC6,   "Barnes Group Inc." },
    { 0x0AC7,   "Chengdu Aich Technology Co.,Ltd" },
    { 0x0AC8,   "Keepin Co., Ltd." },
    { 0x0AC9,   "Swedlock AB" },
    { 0x0ACA,   "Shenzhen CoolKit Technology Co., Ltd" },
    { 0x0ACB,   "ise Individuelle Software und Elektronik GmbH" },
    { 0x0ACC,   "Nuvoton" },
    { 0x0ACD,   "Visuallex Sport International Limited" },
    { 0x0ACE,   "KOBATA GAUGE MFG. CO., LTD." },
    { 0x0ACF,   "CACI Technologies" },
    { 0x0AD0,   "Nordic Strong ApS" },
    { 0x0AD1,   "EAGLE KINGDOM TECHNOLOGIES LIMITED" },
    { 0x0AD2,   "Lautsprecher Teufel GmbH" },
    { 0x0AD3,   "SSV Software Systems GmbH" },
    { 0x0AD4,   "Zhuhai Pantum Electronisc Co., Ltd" },
    { 0x0AD5,   "Streamit B.V." },
    { 0x0AD6,   "nymea GmbH" },
    { 0x0AD7,   "AL-KO Geraete GmbH" },
    { 0x0AD8,   "Franz Kaldewei GmbH&Co KG" },
    { 0x0AD9,   "Shenzhen Aimore. Co.,Ltd" },
    { 0x0ADA,   "Codefabrik GmbH" },
    { 0x0ADB,   "Reelables, Inc." },
    { 0x0ADC,   "Duravit AG" },
    { 0x0ADD,   "Boss Audio" },
    { 0x0ADE,   "Vocera Communications, Inc." },
    { 0x0ADF,   "Douglas Dynamics L.L.C." },
    { 0x0AE0,   "Viceroy Devices Corporation" },
    { 0x0AE1,   "ChengDu ForThink Technology Co., Ltd." },
    { 0x0AE2,   "IMATRIX SYSTEMS, INC." },
    { 0x0AE3,   "GlobalMed" },
    { 0x0AE4,   "DALI Alliance" },
    { 0x0AE5,   "unu GmbH" },
    { 0x0AE6,   "Hexology" },
    { 0x0AE7,   "Sunplus Technology Co., Ltd." },
    { 0x0AE8,   "LEVEL, s.r.o." },
    { 0x0AE9,   "FLIR Systems AB" },
    { 0x0AEA,   "Borda Technology" },
    { 0x0AEB,   "Square, Inc." },
    { 0x0AEC,   "FUTEK ADVANCED SENSOR TECHNOLOGY, INC" },
    { 0x0AED,   "Saxonar GmbH" },
    { 0x0AEE,   "Velentium, LLC" },
    { 0x0AEF,   "GLP German Light Products GmbH" },
    { 0x0AF0,   "Leupold & Stevens, Inc." },
    { 0x0AF1,   "CRADERS,CO.,LTD" },
    { 0x0AF2,   "Shanghai All Link Microelectronics Co.,Ltd" },
    { 0x0AF3,   "701x Inc." },
    { 0x0AF4,   "Radioworks Microelectronics PTY LTD" },
    { 0x0AF5,   "Unitech Electronic Inc." },
    { 0x0AF6,   "AMETEK, Inc." },
    { 0x0AF7,   "Irdeto" },
    { 0x0AF8,   "First Design System Inc." },
    { 0x0AF9,   "Unisto AG" },
    { 0x0AFA,   "Chengdu Ambit Technology Co., Ltd." },
    { 0x0AFB,   "SMT ELEKTRONIK GmbH" },
    { 0x0AFC,   "Cerebrum Sensor Technologies Inc." },
    { 0x0AFD,   "Weber Sensors, LLC" },
    { 0x0AFE,   "Earda Technologies Co.,Ltd" },
    { 0x0AFF,   "FUSEAWARE LIMITED" },
    { 0x0B00,   "Flaircomm Microelectronics Inc." },
    { 0x0B01,   "RESIDEO TECHNOLOGIES, INC." },
    { 0x0B02,   "IORA Technology Development Ltd. Sti." },
    { 0x0B03,   "Precision Triathlon Systems Limited" },
    { 0x0B04,   "I-PERCUT" },
    { 0x0B05,   "Marquardt GmbH" },
    { 0x0B06,   "FAZUA GmbH" },
    { 0x0B07,   "Workaround Gmbh" },
    { 0x0B08,   "Shenzhen Qianfenyi Intelligent Technology Co., LTD" },
    { 0x0B09,   "soonisys" },
    { 0x0B0A,   "Belun Technology Company Limited" },
    { 0x0B0B,   "Sanistaal A/S" },
    { 0x0B0C,   "BluPeak" },
    { 0x0B0D,   "SANYO DENKO Co.,Ltd." },
    { 0x0B0E,   "Honda Lock Mfg. Co.,Ltd." },
    { 0x0B0F,   "B.E.A. S.A." },
    { 0x0B10,   "Alfa Laval Corporate AB" },
    { 0x0B11,   "ThermoWorks, Inc." },
    { 0x0B12,   "ToughBuilt Industries LLC" },
    { 0x0B13,   "IOTOOLS" },
    { 0x0B14,   "Olumee" },
    { 0x0B15,   "NAOS JAPAN K.K." },
    { 0x0B16,   "Guard RFID Solutions Inc." },
    { 0x0B17,   "SIG SAUER, INC." },
    { 0x0B18,   "DECATHLON SE" },
    { 0x0B19,   "WBS PROJECT H PTY LTD" },
    { 0x0B1A,   "Roca Sanitario, S.A." },
    { 0x0B1B,   "Enerpac Tool Group Corp." },
    { 0x0B1C,   "Nanoleq AG" },
    { 0x0B1D,   "Accelerated Systems" },
    { 0x0B1E,   "PB INC." },
    { 0x0B1F,   "Beijing ESWIN Computing Technology Co., Ltd." },
    { 0x0B20,   "TKH Security B.V." },
    { 0x0B21,   "ams AG" },
    { 0x0B22,   "Hygiene IQ, LLC." },
    { 0x0B23,   "iRhythm Technologies, Inc." },
    { 0x0B24,   "BeiJing ZiJie TiaoDong KeJi Co.,Ltd." },
    { 0x0B25,   "NIBROTECH LTD" },
    { 0x0B26,   "Baracoda Daily Healthtech." },
    { 0x0B27,   "Lumi United Technology Co., Ltd" },
    { 0x0B28,   "CHACON" },
    { 0x0B29,   "Tech-Venom Entertainment Private Limited" },
    { 0x0B2A,   "ACL Airshop B.V." },
    { 0x0B2B,   "MAINBOT" },
    { 0x0B2C,   "ILLUMAGEAR, Inc." },
    { 0x0B2D,   "REDARC ELECTRONICS PTY LTD" },
    { 0x0B2E,   "MOCA System Inc." },
    { 0x0B2F,   "Duke Manufacturing Co" },
    { 0x0B30,   "ART SPA" },
    { 0x0B31,   "Silver Wolf Vehicles Inc." },
    { 0x0B32,   "Hala Systems, Inc." },
    { 0x0B33,   "ARMATURA LLC" },
    { 0x0B34,   "CONZUMEX INDUSTRIES PRIVATE LIMITED" },
    { 0x0B35,   "BH SENS" },
    { 0x0B36,   "SINTEF" },
    { 0x0B37,   "Omnivoltaic Energy Solutions Limited Company" },
    { 0x0B38,   "WISYCOM S.R.L." },
    { 0x0B39,   "Red 100 Lighting Co., ltd." },
    { 0x0B3A,   "Impact Biosystems, Inc." },
    { 0x0B3B,   "AIC semiconductor (Shanghai) Co., Ltd." },
    { 0x0B3C,   "Dodge Industrial, Inc." },
    { 0x0B3D,   "REALTIMEID AS" },
    { 0x0B3E,   "ISEO Serrature S.p.a." },
    { 0x0B3F,   "MindRhythm, Inc." },
    { 0x0B40,   "Havells India Limited" },
    { 0x0B41,   "Sentrax GmbH" },
    { 0x0B42,   "TSI" },
    { 0x0B43,   "INCITAT ENVIRONNEMENT" },
    { 0x0B44,   "nFore Technology Co., Ltd." },
    { 0x0B45,   "Electronic Sensors, Inc." },
    { 0x0B46,   "Bird Rides, Inc." },
    { 0x0B47,   "Gentex Corporation" },
    { 0x0B48,   "NIO USA, Inc." },
    { 0x0B49,   "SkyHawke Technologies" },
    { 0x0B4A,   "Nomono AS" },
    { 0x0B4B,   "EMS Integrators, LLC" },
    { 0x0B4C,   "BiosBob.Biz" },
    { 0x0B4D,   "Adam Hall GmbH" },
    { 0x0B4E,   "ICP Systems B.V." },
    { 0x0B4F,   "Breezi.io, Inc." },
    { 0x0B50,   "Mesh Systems LLC" },
    { 0x0B51,   "FUN FACTORY GmbH" },
    { 0x0B52,   "ZIIP Inc" },
    { 0x0B53,   "SHENZHEN KAADAS INTELLIGENT TECHNOLOGY CO.,Ltd" },
    { 0x0B54,   "Emotion Fitness GmbH & Co. KG" },
    { 0x0B55,   "H G M Automotive Electronics, Inc." },
    { 0x0B56,   "BORA - Vertriebs GmbH & Co KG" },
    { 0x0B57,   "CONVERTRONIX TECHNOLOGIES AND SERVICES LLP" },
    { 0x0B58,   "TOKAI-DENSHI INC" },
    { 0x0B59,   "Versa Group B.V." },
    { 0x0B5A,   "H.P. Shelby Manufacturing, LLC." },
    { 0x0B5B,   "Shenzhen ImagineVision Technology Limited" },
    { 0x0B5C,   "Exponential Power, Inc." },
    { 0x0B5D,   "Fujian Newland Auto-ID Tech. Co., Ltd." },
    { 0x0B5E,   "CELLCONTROL, INC." },
    { 0x0B5F,   "Rivieh, Inc." },
    { 0x0B60,   "RATOC Systems, Inc." },
    { 0x0B61,   "Sentek Pty Ltd" },
    { 0x0B62,   "NOVEA ENERGIES" },
    { 0x0B63,   "Innolux Corporation" },
    { 0x0B64,   "NingBo klite Electric Manufacture Co.,LTD" },
    { 0x0B65,   "The Apache Software Foundation" },
    { 0x0B66,   "MITSUBISHI ELECTRIC AUTOMATION (THAILAND) COMPANY LIMITED" },
    { 0x0B67,   "CleanSpace Technology Pty Ltd" },
    { 0x0B68,   "Quha oy" },
    { 0x0B69,   "Addaday" },
    { 0x0B6A,   "Dymo" },
    { 0x0B6B,   "Samsara Networks, Inc" },
    { 0x0B6C,   "Sensitech, Inc." },
    { 0x0B6D,   "SOLUM CO., LTD" },
    { 0x0B6E,   "React Mobile" },
    { 0x0B6F,   "Shenzhen Malide Technology Co.,Ltd" },
    { 0x0B70,   "JDRF Electromag Engineering Inc" },
    { 0x0B71,   "lilbit ODM AS" },
    { 0x0B72,   "Geeknet, Inc." },
    { 0x0B73,   "HARADA INDUSTRY CO., LTD." },
    { 0x0B74,   "BQN" },
    { 0x0B75,   "Triple W Japan Inc." },
    { 0x0B76,   "MAX-co., ltd" },
    { 0x0B77,   "Aixlink(Chengdu) Co., Ltd." },
    { 0x0B78,   "FIELD DESIGN INC." },
    { 0x0B79,   "Sankyo Air Tech Co.,Ltd." },
    { 0x0B7A,   "Shenzhen KTC Technology Co.,Ltd." },
    { 0x0B7B,   "Hardcoder Oy" },
    { 0x0B7C,   "Scangrip A/S" },
    { 0x0B7D,   "FoundersLane GmbH" },
    { 0x0B7E,   "Offcode Oy" },
    { 0x0B7F,   "ICU tech GmbH" },
    { 0x0B80,   "AXELIFE" },
    { 0x0B81,   "SCM Group" },
    { 0x0B82,   "Mammut Sports Group AG" },
    { 0x0B83,   "Taiga Motors Inc." },
    { 0x0B84,   "Presidio Medical, Inc." },
    { 0x0B85,   "VIMANA TECH PTY LTD" },
    { 0x0B86,   "Trek Bicycle" },
    { 0x0B87,   "Ampetronic Ltd" },
    { 0x0B88,   "Muguang (Guangdong) Intelligent Lighting Technology Co., Ltd" },
    { 0x0B89,   "Rotronic AG" },
    { 0x0B8A,   "Seiko Instruments Inc." },
    { 0x0B8B,   "American Technology Components, Incorporated" },
    { 0x0B8C,   "MOTREX" },
    { 0x0B8D,   "Pertech Industries Inc" },
    { 0x0B8E,   "Gentle Energy Corp." },
    { 0x0B8F,   "Senscomm Semiconductor Co., Ltd." },
    { 0x0B90,   "Ineos Automotive Limited" },
    { 0x0B91,   "Alfen ICU B.V." },
    { 0x0B92,   "Citisend Solutions, SL" },
    { 0x0B93,   "Hangzhou BroadLink Technology Co., Ltd." },
    { 0x0B94,   "Dreem SAS" },
    { 0x0B95,   "Netwake GmbH" },
    { 0x0B96,   "Telecom Design" },
    { 0x0B97,   "SILVER TREE LABS, INC." },
    { 0x0B98,   "Gymstory B.V." },
    { 0x0B99,   "The Goodyear Tire & Rubber Company" },
    { 0x0B9A,   "Beijing Wisepool Infinite Intelligence Technology Co.,Ltd" },
    { 0x0B9B,   "GISMAN" },
    { 0x0B9C,   "Komatsu Ltd." },
    { 0x0B9D,   "Sensoria Holdings LTD" },
    { 0x0B9E,   "Audio Partnership Plc" },
    { 0x0B9F,   "Group Lotus Limited" },
    { 0x0BA0,   "Data Sciences International" },
    { 0x0BA1,   "Bunn-O-Matic Corporation" },
    { 0x0BA2,   "TireCheck GmbH" },
    { 0x0BA3,   "Sonova Consumer Hearing GmbH" },
    { 0x0BA4,   "Vervent Audio Group" },
    { 0x0BA5,   "SONICOS ENTERPRISES, LLC" },
    { 0x0BA6,   "Nissan Motor Co., Ltd." },
    { 0x0BA7,   "hearX Group (Pty) Ltd" },
    { 0x0BA8,   "GLOWFORGE INC." },
    { 0x0BA9,   "Allterco Robotics ltd" },
    { 0x0BAA,   "Infinitegra, Inc." },
    { 0x0BAB,   "Grandex International Corporation" },
    { 0x0BAC,   "Machfu Inc." },
    { 0x0BAD,   "Roambotics, Inc." },
    { 0x0BAE,   "Soma Labs LLC" },
    { 0x0BAF,   "NITTO KOGYO CORPORATION" },
    { 0x0BB0,   "Ecolab Inc." },
    { 0x0BB1,   "Beijing ranxin intelligence technology Co.,LTD" },
    { 0x0BB2,   "Fjorden Electra AS" },
    { 0x0BB3,   "Flender GmbH" },
    { 0x0BB4,   "New Cosmos USA, Inc." },
    { 0x0BB5,   "Xirgo Technologies, LLC" },
    { 0x0BB6,   "Build With Robots Inc." },
    { 0x0BB7,   "IONA Tech LLC" },
    { 0x0BB8,   "INNOVAG PTY. LTD." },
    { 0x0BB9,   "SaluStim Group Oy" },
    { 0x0BBA,   "Huso, INC" },
    { 0x0BBB,   "SWISSINNO SOLUTIONS AG" },
    { 0x0BBC,   "T2REALITY SOLUTIONS PRIVATE LIMITED" },
    { 0x0BBD,   "ETHEORY PTY LTD" },
    { 0x0BBE,   "SAAB Aktiebolag" },
    { 0x0BBF,   "HIMSA II K/S" },
    { 0x0BC0,   "READY FOR SKY LLP" },
    { 0x0BC1,   "Miele & Cie. KG" },
    { 0x0BC2,   "EntWick Co." },
    { 0x0BC3,   "MCOT INC." },
    { 0x0BC4,   "TECHTICS ENGINEERING B.V." },
    { 0x0BC5,   "Aperia Technologies, Inc." },
    { 0x0BC6,   "TCL COMMUNICATION EQUIPMENT CO.,LTD." },
    { 0x0BC7,   "Signtle Inc." },
    { 0x0BC8,   "OTF Distribution, LLC" },
    { 0x0BC9,   "Neuvatek Inc." },
    { 0x0BCA,   "Perimeter Technologies, Inc." },
    { 0x0BCB,   "Divesoft s.r.o." },
    { 0x0BCC,   "Sylvac sa" },
    { 0x0BCD,   "Amiko srl" },
    { 0x0BCE,   "Neurosity, Inc." },
    { 0x0BCF,   "LL Tec Group LLC" },
    { 0x0BD0,   "Durag GmbH" },
    { 0x0BD1,   "Hubei Yuan Times Technology Co., Ltd." },
    { 0x0BD2,   "IDEC" },
    { 0x0BD3,   "Procon Analytics, LLC" },
    { 0x0BD4,   "ndd Medizintechnik AG" },
    { 0x0BD5,   "Super B Lithium Power B.V." },
    { 0x0BD6,   "Shenzhen Injoinic Technology Co., Ltd." },
    { 0x0BD7,   "VINFAST TRADING AND PRODUCTION JOINT STOCK COMPANY" },
    { 0x0BD8,   "PURA SCENTS, INC." },
    { 0x0BD9,   "Elics Basis Ltd." },
    { 0x0BDA,   "Aardex Ltd." },
    { 0x0BDB,   "CHAR-BROIL, LLC" },
    { 0x0BDC,   "Ledworks S.r.l." },
    { 0x0BDD,   "Coroflo Limited" },
    { 0x0BDE,   "Yale" },
    { 0x0BDF,   "WINKEY ENTERPRISE (HONG KONG) LIMITED" },
    { 0x0BE0,   "Koizumi Lighting Technology corp." },
    { 0x0BE1,   "Back40 Precision" },
    { 0x0BE2,   "OTC engineering" },
    { 0x0BE3,   "Comtel Systems Ltd." },
    { 0x0BE4,   "Deepfield Connect GmbH" },
    { 0x0BE5,   "ZWILLING J.A. Henckels Aktiengesellschaft" },
    { 0x0BE6,   "Puratap Pty Ltd" },
    { 0x0BE7,   "Fresnel Technologies, Inc." },
    { 0x0BE8,   "Sensormate AG" },
    { 0x0BE9,   "Shindengen Electric Manufacturing Co., Ltd." },
    { 0x0BEA,   "Twenty Five Seven, prodaja in storitve, d.o.o." },
    { 0x0BEB,   "Luna Health, Inc." },
    { 0x0BEC,   "Miracle-Ear, Inc." },
    { 0x0BED,   "CORAL-TAIYI Co. Ltd." },
    { 0x0BEE,   "LINKSYS USA, INC." },
    { 0x0BEF,   "Safetytest GmbH" },
    { 0x0BF0,   "KIDO SPORTS CO., LTD." },
    { 0x0BF1,   "Site IQ LLC" },
    { 0x0BF2,   "Angel Medical Systems, Inc." },
    { 0x0BF3,   "PONE BIOMETRICS AS" },
    { 0x0BF4,   "ER Lab LLC" },
    { 0x0BF5,   "T5 tek, Inc." },
    { 0x0BF6,   "greenTEG AG" },
    { 0x0BF7,   "Wacker Neuson SE" },
    { 0x0BF8,   "Innovacionnye Resheniya" },
    { 0x0BF9,   "Alio, Inc" },
    { 0x0BFA,   "CleanBands Systems Ltd." },
    { 0x0BFB,   "Dodam Enersys Co., Ltd" },
    { 0x0BFC,   "T+A elektroakustik GmbH & Co.KG" },
    { 0x0BFD,   "Esmé Solutions" },
    { 0x0BFE,   "Media-Cartec GmbH" },
    { 0x0BFF,   "Ratio Electric BV" },
    { 0x0C00,   "MQA Limited" },
    { 0x0C01,   "NEOWRK SISTEMAS INTELIGENTES S.A." },
    { 0x0C02,   "Loomanet, Inc." },
    { 0x0C03,   "Puff Corp" },
    { 0x0C04,   "Happy Health, Inc." },
    { 0x0C05,   "Montage Connect, Inc." },
    { 0x0C06,   "LED Smart Inc." },
    { 0x0C07,   "CONSTRUKTS, INC." },
    { 0x0C08,   "limited liability company \"Red\"" },
    { 0x0C09,   "Senic Inc." },
    { 0x0C0A,   "Automated Pet Care Products, LLC" },
    { 0x0C0B,   "aconno GmbH" },
    { 0x0C0C,   "Mendeltron, Inc." },
    { 0x0C0D,   "Mereltron bv" },
    { 0x0C0E,   "ALEX DENKO CO.,LTD." },
    { 0x0C0F,   "AETERLINK" },
    { 0x0C10,   "Cosmed s.r.l." },
    { 0x0C11,   "Gordon Murray Design Limited" },
    { 0x0C12,   "IoSA" },
    { 0x0C13,   "Scandinavian Health Limited" },
    { 0x0C14,   "Fasetto, Inc." },
    { 0x0C15,   "Geva Sol B.V." },
    { 0x0C16,   "TYKEE PTY. LTD." },
    { 0x0C17,   "SomnoMed Limited" },
    { 0x0C18,   "CORROHM" },
    { 0x0C19,   "Arlo Technologies, Inc." },
    { 0x0C1A,   "Catapult Group International Ltd" },
    { 0x0C1B,   "Rockchip Electronics Co., Ltd." },
    { 0x0C1C,   "GEMU" },
    { 0x0C1D,   "OFF Line Japan Co., Ltd." },
    { 0x0C1E,   "EC sense co., Ltd" },
    { 0x0C1F,   "LVI Co." },
    { 0x0C20,   "COMELIT GROUP S.P.A." },
    { 0x0C21,   "Foshan Viomi Electrical Technology Co., Ltd" },
    { 0x0C22,   "Glamo Inc." },
    { 0x0C23,   "KEYTEC,Inc." },
    { 0x0C24,   "SMARTD TECHNOLOGIES INC." },
    { 0x0C25,   "JURA Elektroapparate AG" },
    { 0x0C26,   "Performance Electronics, Ltd." },
    { 0x0C27,   "Pal Electronics" },
    { 0x0C28,   "Embecta Corp." },
    { 0x0C29,   "DENSO AIRCOOL CORPORATION" },
    { 0x0C2A,   "Caresix Inc." },
    { 0x0C2B,   "GigaDevice Semiconductor Inc." },
    { 0x0C2C,   "Zeku Technology (Shanghai) Corp., Ltd." },
    { 0x0C2D,   "OTF Product Sourcing, LLC" },
    { 0x0C2E,   "Easee AS" },
    { 0x0C2F,   "BEEHERO, INC." },
    { 0x0C30,   "McIntosh Group Inc" },
    { 0x0C31,   "KINDOO LLP" },
    { 0x0C32,   "Xian Yisuobao Electronic Technology Co., Ltd." },
    { 0x0C33,   "Exeger Operations AB" },
    { 0x0C34,   "BYD Company Limited" },
    { 0x0C35,   "Thermokon-Sensortechnik GmbH" },
    { 0x0C36,   "Cosmicnode BV" },
    { 0x0C37,   "SignalQuest, LLC" },
    { 0x0C38,   "Noritz Corporation." },
    { 0x0C39,   "TIGER CORPORATION" },
    { 0x0C3A,   "Equinosis, LLC" },
    { 0x0C3B,   "ORB Innovations Ltd" },
    { 0x0C3C,   "Classified Cycling" },
    { 0x0C3D,   "Wrmth Corp." },
    { 0x0C3E,   "BELLDESIGN Inc." },
    { 0x0C3F,   "Stinger Equipment, Inc." },
    { 0x0C40,   "HORIBA, Ltd." },
    { 0x0C41,   "Control Solutions LLC" },
    { 0x0C42,   "Heath Consultants Inc." },
    { 0x0C43,   "Berlinger & Co. AG" },
    { 0x0C44,   "ONCELABS LLC" },
    { 0x0C45,   "Brose Verwaltung SE, Bamberg" },
    { 0x0C46,   "Granwin IoT Technology (Guangzhou) Co.,Ltd" },
    { 0x0C47,   "Epsilon Electronics,lnc" },
    { 0x0C48,   "VALEO MANAGEMENT SERVICES" },
    { 0x0C49,   "twopounds gmbh" },
    { 0x0C4A,   "atSpiro ApS" },
    { 0x0C4B,   "ADTRAN, Inc." },
    { 0x0C4C,   "Orpyx Medical Technologies Inc." },
    { 0x0C4D,   "Seekwave Technology Co.,ltd." },
    { 0x0C4E,   "Tactile Engineering, Inc." },
    { 0x0C4F,   "SharkNinja Operating LLC" },
    { 0x0C50,   "Imostar Technologies Inc." },
    { 0x0C51,   "INNOVA S.R.L." },
    { 0x0C52,   "ESCEA LIMITED" },
    { 0x0C53,   "Taco, Inc." },
    { 0x0C54,   "HiViz Lighting, Inc." },
    { 0x0C55,   "Zintouch B.V." },
    { 0x0C56,   "Rheem Sales Company, Inc." },
    { 0x0C57,   "UNEEG medical A/S" },
    { 0x0C58,   "Hykso Inc." },
    { 0x0C59,   "CYBERDYNE Inc." },
    { 0x0C5A,   "Lockswitch Sdn Bhd" },
    { 0x0C5B,   "Alban Giacomo S.P.A." },
    { 0x0C5C,   "MGM WIRELESSS HOLDINGS PTY LTD" },
    { 0x0C5D,   "StepUp Solutions ApS" },
    { 0x0C5E,   "BlueID GmbH" },
    { 0x0C5F,   "Wuxi Linkpower Microelectronics Co.,Ltd" },
    { 0x0C60,   "KEBA Energy Automation GmbH" },
    { 0x0C61,   "NNOXX, Inc" },
    { 0x0C62,   "Phiaton Corporation" },
    { 0x0C63,   "phg Peter Hengstler GmbH + Co. KG" },
    { 0x0C64,   "dormakaba Holding AG" },
    { 0x0C65,   "WAKO CO,.LTD" },
    { 0x0C66,   "DEN Smart Home B.V." },
    { 0x0C67,   "TRACKTING S.R.L." },
    { 0x0C68,   "Emerja Corporation" },
    { 0x0C69,   "BLITZ electric motors. LTD" },
    { 0x0C6A,   "CONSORCIO TRUST CONTROL - NETTEL" },
    { 0x0C6B,   "GILSON SAS" },
    { 0x0C6C,   "SNIFF LOGIC LTD" },
    { 0x0C6D,   "Fidure Corp." },
    { 0x0C6E,   "Sensa LLC" },
    { 0x0C6F,   "Parakey AB" },
    { 0x0C70,   "SCARAB SOLUTIONS LTD" },
    { 0x0C71,   "BitGreen Technolabz (OPC) Private Limited" },
    { 0x0C72,   "StreetCar ORV, LLC" },
    { 0x0C73,   "Truma Gerätetechnik GmbH & Co. KG" },
    { 0x0C74,   "yupiteru" },
    { 0x0C75,   "Embedded Engineering Solutions LLC" },
    { 0x0C76,   "Shenzhen Gwell Times Technology Co. , Ltd" },
    { 0x0C77,   "TEAC Corporation" },
    { 0x0C78,   "CHARGTRON IOT PRIVATE LIMITED" },
    { 0x0C79,   "Zhuhai Smartlink Technology Co., Ltd" },
    { 0x0C7A,   "Triductor Technology (Suzhou), Inc." },
    { 0x0C7B,   "PT SADAMAYA GRAHA TEKNOLOGI" },
    { 0x0C7C,   "Mopeka Products LLC" },
    { 0x0C7D,   "3ALogics, Inc." },
    { 0x0C7E,   "BOOMING OF THINGS" },
    { 0x0C7F,   "Rochester Sensors, LLC" },
    { 0x0C80,   "CARDIOID - TECHNOLOGIES, LDA" },
    { 0x0C81,   "Carrier Corporation" },
    { 0x0C82,   "NACON" },
    { 0x0C83,   "Watchdog Systems LLC" },
    { 0x0C84,   "MAXON INDUSTRIES, INC." },
    { 0x0C85,   "Amlogic, Inc." },
    { 0x0C86,   "Qingdao Eastsoft Communication Technology Co.,Ltd" },
    { 0x0C87,   "Weltek Technologies Company Limited" },
    { 0x0C88,   "Nextivity Inc." },
    { 0x0C89,   "AGZZX OPTOELECTRONICS TECHNOLOGY CO., LTD" },
    { 0x0C8A,   "A.GLOBAL co.,Ltd." },
    { 0x0C8B,   "Heavys Inc" },
    { 0x0C8C,   "T-Mobile USA" },
    { 0x0C8D,   "tonies GmbH" },
    { 0x0C8E,   "Technocon Engineering Ltd." },
    { 0x0C8F,   "Radar Automobile Sales(Shandong)Co.,Ltd." },
    { 0x0C90,   "WESCO AG" },
    { 0x0C91,   "Yashu Systems" },
    { 0x0C92,   "Kesseböhmer Ergonomietechnik GmbH" },
    { 0x0C93,   "Movesense Oy" },
    { 0x0C94,   "Baxter Healthcare Corporation" },
    { 0x0C95,   "Gemstone Lights Canada Ltd." },
    { 0x0C96,   "H+B Hightech GmbH" },
    { 0x0C97,   "Deako" },
    { 0x0C98,   "MiX Telematics International (PTY) LTD" },
    { 0x0C99,   "Vire Health Oy" },
    { 0x0C9A,   "ALF Inc." },
    { 0x0C9B,   "NTT sonority, Inc." },
    { 0x0C9C,   "Sunstone-RTLS Ipari Szolgaltato Korlatolt Felelossegu Tarsasag" },
    { 0x0C9D,   "Ribbiot, INC." },
    { 0x0C9E,   "ECCEL CORPORATION SAS" },
    { 0x0C9F,   "Dragonfly Energy Corp." },
    { 0x0CA0,   "BIGBEN" },
    { 0x0CA1,   "YAMAHA MOTOR CO.,LTD." },
    { 0x0CA2,   "XSENSE LTD" },
    { 0x0CA3,   "MAQUET GmbH" },
    { 0x0CA4,   "MITSUBISHI ELECTRIC LIGHTING CO, LTD" },
    { 0x0CA5,   "Princess Cruise Lines, Ltd." },
    { 0x0CA6,   "Megger Ltd" },
    { 0x0CA7,   "Verve InfoTec Pty Ltd" },
    { 0x0CA8,   "Sonas, Inc." },
    { 0x0CA9,   "Mievo Technologies Private Limited" },
    { 0x0CAA,   "Shenzhen Poseidon Network Technology Co., Ltd" },
    { 0x0CAB,   "HERUTU ELECTRONICS CORPORATION" },
    { 0x0CAC,   "Shenzhen Shokz Co.,Ltd." },
    { 0x0CAD,   "Shenzhen Openhearing Tech CO., LTD ." },
    { 0x0CAE,   "Evident Corporation" },
    { 0x0CAF,   "NEURINNOV" },
    { 0x0CB0,   "SwipeSense, Inc." },
    { 0x0CB1,   "RF Creations" },
    { 0x0CB2,   "SHINKAWA Sensor Technology, Inc." },
    { 0x0CB3,   "janova GmbH" },
    { 0x0CB4,   "Eberspaecher Climate Control Systems GmbH" },
    { 0x0CB5,   "Racketry, d. o. o." },
    { 0x0CB6,   "THE EELECTRIC MACARON LLC" },
    { 0x0CB7,   "Cucumber Lighting Controls Limited" },
    { 0x0CB8,   "Shanghai Proxy Network Technology Co., Ltd." },
    { 0x0CB9,   "seca GmbH & Co. KG" },
    { 0x0CBA,   "Ameso Tech (OPC) Private Limited" },
    { 0x0CBB,   "Emlid Tech Kft." },
    { 0x0CBC,   "TROX GmbH" },
    { 0x0CBD,   "Pricer AB" },
    { 0x0CBF,   "Forward Thinking Systems LLC." },
    { 0x0CC0,   "Garnet Instruments Ltd." },
    { 0x0CC1,   "CLEIO Inc." },
    { 0x0CC2,   "Anker Innovations Limited" },
    { 0x0CC3,   "HMD Global Oy" },
    { 0x0CC4,   "ABUS August Bremicker Soehne Kommanditgesellschaft" },
    { 0x0CC5,   "Open Road Solutions, Inc." },
    { 0x0CC6,   "Serial Technology Corporation" },
    { 0x0CC7,   "SB C&S Corp." },
    { 0x0CC8,   "TrikThom" },
    { 0x0CC9,   "Innocent Technology Co., Ltd." },
    { 0x0CCA,   "Cyclops Marine Ltd" },
    { 0x0CCB,   "NOTHING TECHNOLOGY LIMITED" },
    { 0x0CCC,   "Kord Defence Pty Ltd" },
    { 0x0CCD,   "YanFeng Visteon(Chongqing) Automotive Electronic Co.,Ltd" },
    { 0x0CCE,   "SENOSPACE LLC" },
    { 0x0CCF,   "Shenzhen CESI Information Technology Co., Ltd." },
    { 0x0CD0,   "MooreSilicon Semiconductor Technology (Shanghai) Co., LTD." },
    { 0x0CD1,   "Imagine Marketing Limited" },
    { 0x0CD2,   "EQOM SSC B.V." },
    { 0x0CD3,   "TechSwipe" },
    { 0x0CD4,   "Reoqoo IoT Technology Co., Ltd." },
    { 0x0CD5,   "Numa Products, LLC" },
    { 0x0CD6,   "HHO (Hangzhou) Digital Technology Co., Ltd." },
    { 0x0CD7,   "Maztech Industries, LLC" },
    { 0x0CD8,   "SIA Mesh Group" },
    { 0x0CD9,   "Minami acoustics Limited" },
    { 0x0CDA,   "Wolf Steel ltd" },
    { 0x0CDB,   "Circus World Displays Limited" },
    { 0x0CDC,   "Ypsomed AG" },
    { 0x0CDD,   "Alif Semiconductor, Inc." },
    { 0x0CDE,   "RESPONSE TECHNOLOGIES, LTD." },
    { 0x0CDF,   "SHENZHEN CHENYUN ELECTRONICS  CO., LTD" },
    { 0x0CE0,   "VODALOGIC PTY LTD" },
    { 0x0CE1,   "Regal Beloit America, Inc." },
    { 0x0CE2,   "CORVENT MEDICAL, INC." },
    { 0x0CE3,   "Taiwan Fuhsing" },
    { 0x0CE4,   "Off-Highway Powertrain Services Germany GmbH" },
    { 0x0CE5,   "Amina Distribution AS" },
    { 0x0CE6,   "McWong International, Inc." },
    { 0x0CE7,   "TAG HEUER SA" },
    { 0x0CE8,   "Dongguan Yougo Electronics Co.,Ltd." },
    { 0x0CE9,   "PEAG, LLC dba JLab Audio" },
    { 0x0CEA,   "HAYWARD INDUSTRIES, INC." },
    { 0x0CEB,   "Shenzhen Tingting Technology Co. LTD" },
    { 0x0CEC,   "Pacific Coast Fishery Services (2003) Inc." },
    { 0x0CED,   "CV. NURI TEKNIK" },
    { 0x0CEE,   "MadgeTech, Inc" },
    { 0x0CEF,   "POGS B.V." },
    { 0x0CF0,   "THOTAKA TEKHNOLOGIES INDIA PRIVATE LIMITED" },
    { 0x0CF1,   "Midmark" },
    { 0x0CF2,   "BestSens AG" },
    { 0x0CF3,   "Radio Sound" },
    { 0x0CF4,   "SOLUX PTY LTD" },
    { 0x0CF5,   "BOS Balance of Storage Systems AG" },
    { 0x0CF6,   "OJ Electronics A/S" },
    { 0x0CF7,   "TVS Motor Company Ltd." },
    { 0x0CF8,   "core sensing GmbH" },
    { 0x0CF9,   "Tamblue Oy" },
    { 0x0CFA,   "Protect Animals With Satellites LLC" },
    { 0x0CFB,   "Tyromotion GmbH" },
    { 0x0CFC,   "ElectronX design" },
    { 0x0CFD,   "Wuhan Woncan Construction Technologies Co., Ltd." },
    { 0x0CFE,   "Thule Group AB" },
    { 0x0CFF,   "Ergodriven Inc" },
    { 0x0D00,   "Sparkpark AS" },
    { 0x0D01,   "KEEPEN" },
    { 0x0D02,   "Rocky Mountain ATV/MC Jake Wilson" },
    { 0x0D03,   "MakuSafe Corp" },
    { 0x0D04,   "Bartec Auto Id Ltd" },
    { 0x0D05,   "Energy Technology and Control Limited" },
    { 0x0D06,   "doubleO Co., Ltd." },
    { 0x0D07,   "Datalogic S.r.l." },
    { 0x0D08,   "Datalogic USA, Inc." },
    { 0x0D09,   "Leica Geosystems AG" },
    { 0x0D0A,   "CATEYE Co., Ltd." },
    { 0x0D0B,   "Research Products Corporation" },
    { 0x0D0C,   "Planmeca Oy" },
    { 0x0D0D,   "C.Ed. Schulte GmbH Zylinderschlossfabrik" },
    { 0x0D0E,   "PetVoice Co., Ltd." },
    { 0x0D0F,   "Timebirds Australia Pty Ltd" },
    { 0x0D10,   "JVC KENWOOD Corporation" },
    { 0x0D11,   "Great Dane LLC" },
    { 0x0D12,   "Spartek Systems Inc." },
    { 0x0D13,   "MERRY ELECTRONICS CO., LTD." },
    { 0x0D14,   "Merry Electronics (S) Pte Ltd" },
    { 0x0D15,   "Spark" },
    { 0x0D16,   "Nations Technologies Inc." },
    { 0x0D17,   "Akix S.r.l." },
    { 0x0D18,   "Bioliberty Ltd" },
    { 0x0D19,   "C.G. Air Systemes Inc." },
    { 0x0D1A,   "Maturix ApS" },
    { 0x0D1B,   "RACHIO, INC." },
    { 0x0D1C,   "LIMBOID LLC" },
    { 0x0D1D,   "Electronics4All Inc." },
    { 0x0D1E,   "FESTINA LOTUS SA" },
    { 0x0D1F,   "Synkopi, Inc." },
    { 0x0D20,   "SCIENTERRA LIMITED" },
    { 0x0D21,   "Cennox Group Limited" },
    { 0x0D22,   "Cedarware, Corp." },
    { 0x0D23,   "GREE Electric Appliances, Inc. of Zhuhai" },
    { 0x0D24,   "Japan Display Inc." },
    { 0x0D25,   "System Elite Holdings Group Limited" },
    { 0x0D26,   "Burkert Werke GmbH & Co. KG" },
    { 0x0D27,   "velocitux" },
    { 0x0D28,   "FUJITSU COMPONENT LIMITED" },
    { 0x0D29,   "MIYAKAWA ELECTRIC WORKS LTD." },
    { 0x0D2A,   "PhysioLogic Devices, Inc." },
    { 0x0D2B,   "Sensoryx AG" },
    { 0x0D2C,   "SIL System Integration Laboratory GmbH" },
    { 0x0D2D,   "Cooler Pro, LLC" },
    { 0x0D2E,   "Advanced Electronic Applications, Inc" },
    { 0x0D2F,   "Delta Development Team, Inc" },
    { 0x0D30,   "Laxmi Therapeutic Devices, Inc." },
    { 0x0D31,   "SYNCHRON, INC." },
    { 0x0D32,   "Badger Meter" },
    { 0x0D33,   "Micropower Group AB" },
    { 0x0D34,   "ZILLIOT TECHNOLOGIES PRIVATE LIMITED" },
    { 0x0D35,   "Universidad Politecnica de Madrid" },
    { 0x0D36,   "XIHAO INTELLIGENGT TECHNOLOGY CO., LTD" },
    { 0x0D37,   "Zerene Inc." },
    { 0x0D38,   "CycLock" },
    { 0x0D39,   "Systemic Games, LLC" },
    { 0x0D3A,   "Frost Solutions, LLC" },
    { 0x0D3B,   "Lone Star Marine Pty Ltd" },
    { 0x0D3C,   "SIRONA Dental Systems GmbH" },
    { 0x0D3D,   "bHaptics Inc." },
    { 0x0D3E,   "LUMINOAH, INC." },
    { 0x0D3F,   "Vogels Products B.V." },
    { 0x0D40,   "SignalFire Telemetry, Inc." },
    { 0x0D41,   "CPAC Systems AB" },
    { 0x0D42,   "TEKTRO TECHNOLOGY CORPORATION" },
    { 0x0D43,   "Gosuncn Technology Group Co., Ltd." },
    { 0x0D44,   "Ex Makhina Inc." },
    { 0x0D45,   "Odeon, Inc." },
    { 0x0D46,   "Thales Simulation & Training AG" },
    { 0x0D47,   "Shenzhen DOKE Electronic Co., Ltd" },
    { 0x0D48,   "Vemcon GmbH" },
    { 0x0D49,   "Refrigerated Transport Electronics, Inc." },
    { 0x0D4A,   "Rockpile Solutions, LLC" },
    { 0x0D4B,   "Soundwave Hearing, LLC" },
    { 0x0D4C,   "IotGizmo Corporation" },
    { 0x0D4D,   "Optec, LLC" },
    { 0x0D4E,   "NIKAT SOLUTIONS PRIVATE LIMITED" },
    { 0x0D4F,   "Movano Inc." },
    { 0x0D50,   "NINGBO FOTILE KITCHENWARE CO., LTD." },
    { 0x0D51,   "Genetus inc." },
    { 0x0D52,   "DIVAN TRADING CO., LTD." },
    { 0x0D53,   "Luxottica Group S.p.A" },
    { 0x0D54,   "ISEKI FRANCE S.A.S" },
    { 0x0D55,   "NO CLIMB PRODUCTS LTD" },
    { 0x0D56,   "Wellang.Co,.Ltd" },
    { 0x0D57,   "Nanjing Xinxiangyuan Microelectronics Co., Ltd." },
    { 0x0D58,   "ifm electronic gmbh" },
    { 0x0D59,   "HYUPSUNG MACHINERY ELECTRIC CO., LTD." },
    { 0x0D5A,   "Gunnebo Aktiebolag" },
    { 0x0D5B,   "Axis Communications AB" },
    { 0x0D5C,   "Pison Technology, Inc." },
    { 0x0D5D,   "Stogger B.V." },
    { 0x0D5E,   "Pella Corp" },
    { 0x0D5F,   "SiChuan Homme Intelligent Technology co.,Ltd." },
    { 0x0D60,   "Smart Products Connection, S.A." },
    { 0x0D61,   "F.I.P. FORMATURA INIEZIONE POLIMERI - S.P.A." },
    { 0x0D62,   "MEBSTER s.r.o." },
    { 0x0D63,   "SKF France" },
    { 0x0D64,   "Southco" },
    { 0x0D65,   "Molnlycke Health Care AB" },
    { 0x0D66,   "Hendrickson USA , L.L.C" },
    { 0x0D67,   "BLACK BOX NETWORK SERVICES INDIA PRIVATE LIMITED" },
    { 0x0D68,   "Status Audio LLC" },
    { 0x0D69,   "AIR AROMA INTERNATIONAL PTY LTD" },
    { 0x0D6A,   "Helge Kaiser GmbH" },
    { 0x0D6B,   "Crane Payment Innovations, Inc." },
    { 0x0D6C,   "Ambient IoT Pty Ltd" },
    { 0x0D6D,   "DYNAMOX S/A" },
    { 0x0D6E,   "Look Cycle International" },
    { 0x0D6F,   "Closed Joint Stock Company NVP BOLID" },
    { 0x0D70,   "Kindhome" },
    { 0x0D71,   "Kiteras Inc." },
    { 0x0D72,   "Earfun Technology (HK) Limited" },
    { 0x0D73,   "iota Biosciences, Inc." },
    { 0x0D74,   "ANUME s.r.o." },
    { 0x0D75,   "Indistinguishable From Magic, Inc." },
    { 0x0D76,   "i-focus Co.,Ltd" },
    { 0x0D77,   "DualNetworks SA" },
    { 0x0D78,   "MITACHI CO.,LTD." },
    { 0x0D79,   "VIVIWARE JAPAN, Inc." },
    { 0x0D7A,   "Xiamen Intretech Inc." },
    { 0x0D7B,   "MindMaze SA" },
    { 0x0D7C,   "BeiJing SmartChip Microelectronics Technology Co.,Ltd" },
    { 0x0D7D,   "Taiko Audio B.V." },
    { 0x0D7E,   "Daihatsu Motor Co., Ltd." },
    { 0x0D7F,   "Konova" },
    { 0x0D80,   "Gravaa B.V." },
    { 0x0D81,   "Beyerdynamic GmbH & Co. KG" },
    { 0x0D82,   "VELCO" },
    { 0x0D83,   "ATLANTIC SOCIETE FRANCAISE DE DEVELOPPEMENT THERMIQUE" },
    { 0x0D84,   "Testo SE & Co. KGaA" },
    { 0x0D85,   "SEW-EURODRIVE GmbH & Co KG" },
    { 0x0D86,   "ROCKWELL AUTOMATION, INC." },
    { 0x0D87,   "Quectel Wireless Solutions Co., Ltd." },
    { 0x0D88,   "Geocene Inc." },
    { 0x0D89,   "Nanohex Corp" },
    { 0x0D8A,   "Simply Embedded Inc." },
    { 0x0D8B,   "Software Development, LLC" },
    { 0x0D8C,   "Ultimea Technology (Shenzhen) Limited" },
    { 0x0D8D,   "RF Electronics Limited" },
    { 0x0D8E,   "Optivolt Labs, Inc." },
    { 0x0D8F,   "Canon Electronics Inc." },
    { 0x0D90,   "LAAS ApS" },
    { 0x0D91,   "Beamex Oy Ab" },
    { 0x0D92,   "TACHIKAWA CORPORATION" },
    { 0x0D93,   "HagerEnergy GmbH" },
    { 0x0D94,   "Shrooly Inc" },
    { 0x0D95,   "Hunter Industries Incorporated" },
    { 0x0D96,   "NEOKOHM SISTEMAS ELETRONICOS LTDA" },
    { 0x0D97,   "Zhejiang Huanfu Technology Co., LTD" },
    { 0x0D98,   "E.F. Johnson Company" },
    { 0x0D99,   "Caire Inc." },
    { 0x0D9A,   "Yeasound (Xiamen) Hearing Technology Co., Ltd" },
    { 0x0D9B,   "Boxyz, Inc." },
    { 0x0D9C,   "Skytech Creations Limited" },
    { 0x0D9D,   "Cear, Inc." },
    { 0x0D9E,   "Impulse Wellness LLC" },
    { 0x0D9F,   "MML US, Inc" },
    { 0x0DA0,   "SICK AG" },
    { 0x0DA1,   "Fen Systems Ltd." },
    { 0x0DA2,   "KIWI.KI GmbH" },
    { 0x0DA3,   "Airgraft Inc." },
    { 0x0DA4,   "HP Tuners" },
    { 0x0DA5,   "PIXELA CORPORATION" },
    { 0x0DA6,   "Generac Corporation" },
    { 0x0DA7,   "Novoferm tormatic GmbH" },
    { 0x0DA8,   "Airwallet ApS" },
    { 0x0DA9,   "Inventronics GmbH" },
    { 0x0DAA,   "Shenzhen EBELONG Technology Co., Ltd." },
    { 0x0DAB,   "Efento" },
    { 0x0DAC,   "ITALTRACTOR ITM S.P.A." },
    { 0x0DAD,   "linktop" },
    { 0x0DAE,   "TITUM AUDIO, INC." },
    { 0x0DAF,   "Hexagon Aura Reality AG" },
    { 0x0DB0,   "Invisalert Solutions, Inc." },
    { 0x0DB1,   "TELE System Communications Pte. Ltd." },
    { 0x0DB2,   "Whirlpool" },
    { 0x0DB3,   "SHENZHEN REFLYING ELECTRONIC CO., LTD" },
    { 0x0DB4,   "Franklin Control Systems" },
    { 0x0DB5,   "Djup AB" },
    { 0x0DB6,   "SAFEGUARD EQUIPMENT, INC." },
    { 0x0DB7,   "Morningstar Corporation" },
    { 0x0DB8,   "Shenzhen Chuangyuan Digital Technology Co., Ltd" },
    { 0x0DB9,   "CompanyDeep Ltd" },
    { 0x0DBA,   "Veo Technologies ApS" },
    { 0x0DBB,   "Nexis Link Technology Co., Ltd." },
    { 0x0DBC,   "Felion Technologies Company Limited" },
    { 0x0DBD,   "MAATEL" },
    { 0x0DBE,   "HELLA GmbH & Co. KGaA" },
    { 0x0DBF,   "HWM-Water Limited" },
    { 0x0DC0,   "Shenzhen Jahport Electronic Technology Co., Ltd." },
    { 0x0DC1,   "NACHI-FUJIKOSHI CORP." },
    { 0x0DC2,   "Cirrus Research plc" },
    { 0x0DC3,   "GEARBAC TECHNOLOGIES INC." },
    { 0x0DC4,   "Hangzhou NationalChip Science & Technology Co.,Ltd" },
    { 0x0DC5,   "DHL" },
    { 0x0DC6,   "Levita" },
    { 0x0DC7,   "MORNINGSTAR FX PTE. LTD." },
    { 0x0DC8,   "ETO GRUPPE TECHNOLOGIES GmbH" },
    { 0x0DC9,   "farmunited GmbH" },
    { 0x0DCA,   "Aptener Mechatronics Private Limited" },
    { 0x0DCB,   "GEOPH, LLC" },
    { 0x0DCC,   "Trotec GmbH" },
    { 0x0DCD,   "Astra LED AG" },
    { 0x0DCE,   "NOVAFON - Electromedical devices limited liability company" },
    { 0x0DCF,   "KUBU SMART LIMITED" },
    { 0x0DD0,   "ESNAH" },
    { 0x0DD1,   "OrangeMicro Limited" },
    { 0x0DD2,   "Sitecom Europe B.V." },
    { 0x0DD3,   "Global Satellite Engineering" },
    { 0x0DD4,   "KOQOON GmbH & Co.KG" },
    { 0x0DD5,   "BEEPINGS" },
    { 0x0DD6,   "MODULAR MEDICAL, INC." },
    { 0x0DD7,   "Xiant Technologies, Inc." },
    { 0x0DD8,   "Granchip IoT Technology (Guangzhou) Co.,Ltd" },
    { 0x0DD9,   "SCHELL GmbH & Co. KG" },
    { 0x0DDA,   "Minebea Intec GmbH" },
    { 0x0DDB,   "KAGA FEI Co., Ltd." },
    { 0x0DDC,   "AUTHOR-ALARM, razvoj in prodaja avtomobilskih sistemov proti kraji, d.o.o." },
    { 0x0DDD,   "Tozoa LLC" },
    { 0x0DDE,   "SHENZHEN DNS INDUSTRIES CO., LTD." },
    { 0x0DDF,   "Shenzhen Lunci Technology Co., Ltd" },
    { 0x0DE0,   "KNOG PTY. LTD." },
    { 0x0DE1,   "Outshiny India Private Limited" },
    { 0x0DE2,   "TAMADIC Co., Ltd." },
    { 0x0DE3,   "Shenzhen MODSEMI Co., Ltd" },
    { 0x0DE4,   "EMBEINT INC" },
    { 0x0DE5,   "Ehong Technology Co.,Ltd" },
    { 0x0DE6,   "DEXATEK Technology LTD" },
    { 0x0DE7,   "Dendro Technologies, Inc." },
    { 0x0DE8,   "Vivint, Inc." },
    { 0xFFFF,   "For use in internal and interoperability tests" },
    {      0,   NULL }
};
value_string_ext bluetooth_company_id_vals_ext = VALUE_STRING_EXT_INIT(bluetooth_company_id_vals);

const value_string bluetooth_address_type_vals[] = {
    { 0x00,  "Public" },
    { 0x01,  "Random" },
    { 0, NULL }
};

/*
 * BLUETOOTH SPECIFICATION Version 4.0 [Vol 5] defines that
 * before transmission, the PAL shall remove the HCI header,
 * add LLC and SNAP headers and insert an 802.11 MAC header.
 * Protocol identifier are described in Table 5.2.
 */

#define AMP_U_L2CAP             0x0001
#define AMP_C_ACTIVITY_REPORT   0x0002
#define AMP_C_SECURITY_FRAME    0x0003
#define AMP_C_LINK_SUP_REQUEST  0x0004
#define AMP_C_LINK_SUP_REPLY    0x0005

static const value_string bluetooth_pid_vals[] = {
    { AMP_U_L2CAP,            "AMP_U L2CAP ACL data" },
    { AMP_C_ACTIVITY_REPORT,  "AMP-C Activity Report" },
    { AMP_C_SECURITY_FRAME,   "AMP-C Security frames" },
    { AMP_C_LINK_SUP_REQUEST, "AMP-C Link supervision request" },
    { AMP_C_LINK_SUP_REPLY,   "AMP-C Link supervision reply" },
    { 0,    NULL }
};

uint32_t bluetooth_max_disconnect_in_frame = UINT32_MAX;


void proto_register_bluetooth(void);
void proto_reg_handoff_bluetooth(void);

/* UAT routines */
static bool
bt_uuids_update_cb(void *r, char **err)
{
    bt_uuid_t *rec = (bt_uuid_t *)r;
    bluetooth_uuid_t uuid;

    if (rec->uuid == NULL) {
        *err = g_strdup("UUID can't be empty");
        return false;
    }
    g_strstrip(rec->uuid);
    if (rec->uuid[0] == 0) {
        *err = g_strdup("UUID can't be empty");
        return false;
    }

    uuid = get_bluetooth_uuid_from_str(rec->uuid);
    if (uuid.size == 0) {
        *err = g_strdup("UUID must be 16, 32, or 128-bit, with the latter formatted as XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX");
        return false;
    }
    /* print_numeric_bluetooth_uuid uses bytes_to_hexstr, which uses
     * lowercase hex digits. */
    rec->uuid = ascii_strdown_inplace(rec->uuid);

    if (rec->label == NULL) {
        *err = g_strdup("UUID Name can't be empty");
        return false;
    }
    g_strstrip(rec->label);
    if (rec->label[0] == 0) {
        *err = g_strdup("UUID Name can't be empty");
        return false;
    }

    *err = NULL;
    return true;
}

static void *
bt_uuids_copy_cb(void* n, const void* o, size_t siz _U_)
{
    bt_uuid_t* new_rec = (bt_uuid_t*)n;
    const bt_uuid_t* old_rec = (const bt_uuid_t*)o;

    new_rec->uuid = g_strdup(old_rec->uuid);
    new_rec->label = g_strdup(old_rec->label);
    new_rec->long_attr = old_rec->long_attr;

    return new_rec;
}

static void
bt_uuids_free_cb(void*r)
{
    bt_uuid_t* rec = (bt_uuid_t*)r;

    const char *found_label;

    found_label = wmem_tree_lookup_string(bluetooth_uuids, rec->uuid, 0);

    if (found_label != NULL && !strcmp(found_label, rec->label)) {
        wmem_tree_remove_string(bluetooth_uuids, rec->uuid, 0);
    }

    g_free(rec->uuid);
    g_free(rec->label);
}

static void
bt_uuids_post_update_cb(void)
{
    if (num_bt_uuids) {
        for (unsigned i = 0; i < num_bt_uuids; i++) {
            wmem_tree_insert_string(bluetooth_uuids, bt_uuids[i].uuid,
                                    &bt_uuids[i], 0);
        }
    }
}

static void
bt_uuids_reset_cb(void)
{
}

UAT_CSTRING_CB_DEF(bt_uuids, uuid, bt_uuid_t)
UAT_CSTRING_CB_DEF(bt_uuids, label, bt_uuid_t)
UAT_BOOL_CB_DEF(bt_uuids, long_attr, bt_uuid_t)

void bluetooth_add_custom_uuid(const char *uuid, const char *label, bool long_attr)
{
    bt_uuid_t* custom_uuid = wmem_new(wmem_epan_scope(), bt_uuid_t);

    custom_uuid->uuid = wmem_strdup(wmem_epan_scope(), uuid);
    custom_uuid->label = wmem_strdup(wmem_epan_scope(), label);
    custom_uuid->long_attr = long_attr;

    // It might make more sense to insert these as UUIDs instead of strings.
    wmem_tree_insert_string(bluetooth_uuids, uuid, custom_uuid, 0);
}

bool bluetooth_get_custom_uuid_long_attr(const bluetooth_uuid_t *uuid)
{
    bt_uuid_t* custom_uuid;
    custom_uuid = wmem_tree_lookup_string(bluetooth_uuids, print_numeric_bluetooth_uuid(wmem_packet_scope(), uuid), 0);
    if (custom_uuid) {
        return custom_uuid->long_attr;
    }
    return false;
}

const char* bluetooth_get_custom_uuid_description(const bluetooth_uuid_t *uuid)
{
    bt_uuid_t* custom_uuid;
    custom_uuid = wmem_tree_lookup_string(bluetooth_uuids, print_numeric_bluetooth_uuid(wmem_packet_scope(), uuid), 0);
    if (custom_uuid) {
        return custom_uuid->label;
    }
    return false;
}

/* Decode As routines */
static void bluetooth_uuid_prompt(packet_info *pinfo, char* result)
{
    char *value_data;

    value_data = (char *) p_get_proto_data(pinfo->pool, pinfo, proto_bluetooth, PROTO_DATA_BLUETOOTH_SERVICE_UUID);
    if (value_data)
        snprintf(result, MAX_DECODE_AS_PROMPT_LEN, "BT Service UUID %s as", (char *) value_data);
    else
        snprintf(result, MAX_DECODE_AS_PROMPT_LEN, "Unknown BT Service UUID");
}

static void *bluetooth_uuid_value(packet_info *pinfo)
{
    char *value_data;

    value_data = (char *) p_get_proto_data(pinfo->pool, pinfo, proto_bluetooth, PROTO_DATA_BLUETOOTH_SERVICE_UUID);

    if (value_data)
        return (void *) value_data;

    return NULL;
}

int
dissect_bd_addr(int hf_bd_addr, packet_info *pinfo, proto_tree *tree,
        tvbuff_t *tvb, int offset, bool is_local_bd_addr,
        uint32_t interface_id, uint32_t adapter_id, uint8_t *bdaddr)
{
    uint8_t bd_addr[6];

    bd_addr[5] = tvb_get_uint8(tvb, offset);
    bd_addr[4] = tvb_get_uint8(tvb, offset + 1);
    bd_addr[3] = tvb_get_uint8(tvb, offset + 2);
    bd_addr[2] = tvb_get_uint8(tvb, offset + 3);
    bd_addr[1] = tvb_get_uint8(tvb, offset + 4);
    bd_addr[0] = tvb_get_uint8(tvb, offset + 5);

    proto_tree_add_ether(tree, hf_bd_addr, tvb, offset, 6, bd_addr);
    offset += 6;

    if (have_tap_listener(bluetooth_device_tap)) {
        bluetooth_device_tap_t  *tap_device;

        tap_device = wmem_new(pinfo->pool, bluetooth_device_tap_t);
        tap_device->interface_id = interface_id;
        tap_device->adapter_id   = adapter_id;
        memcpy(tap_device->bd_addr, bd_addr, 6);
        tap_device->has_bd_addr = true;
        tap_device->is_local = is_local_bd_addr;
        tap_device->type = BLUETOOTH_DEVICE_BD_ADDR;
        tap_queue_packet(bluetooth_device_tap, pinfo, tap_device);
    }

    if (bdaddr)
        memcpy(bdaddr, bd_addr, 6);

    return offset;
}

void bluetooth_unit_0p625_ms(char *buf, uint32_t value) {
    snprintf(buf, ITEM_LABEL_LENGTH, "%g ms (%u slots)", 0.625 * value, value);
}

void bluetooth_unit_1p25_ms(char *buf, uint32_t value) {
    snprintf(buf, ITEM_LABEL_LENGTH, "%g ms (%u slot-pairs)", 1.25 * value, value);
}

void bluetooth_unit_0p01_sec(char *buf, uint32_t value) {
    snprintf(buf, ITEM_LABEL_LENGTH, "%g sec (%u)", 0.01 * value, value);
}

void bluetooth_unit_0p125_ms(char *buf, uint32_t value) {
    snprintf(buf, ITEM_LABEL_LENGTH, "%g ms (%u)", 0.125 * value, value);
}

const value_string bluetooth_procedure_count_special[] = {
    {0x0, "Infinite, Continue until disabled"},
    {0, NULL}
};

const value_string bluetooth_not_supported_0x00_special[] = {
    {0x0, "Not Supported"},
    {0, NULL}
};

const value_string bluetooth_not_used_0xff_special[] = {
    {0xff, "Not used"},
    {0, NULL}
};

void
save_local_device_name_from_eir_ad(tvbuff_t *tvb, int offset, packet_info *pinfo,
        uint8_t size, bluetooth_data_t *bluetooth_data)
{
    int                     i = 0;
    uint8_t                 length;
    wmem_tree_key_t         key[4];
    uint32_t                k_interface_id;
    uint32_t                k_adapter_id;
    uint32_t                k_frame_number;
    char                    *name;
    localhost_name_entry_t  *localhost_name_entry;

    if (!(!pinfo->fd->visited && bluetooth_data)) return;

    while (i < size) {
        length = tvb_get_uint8(tvb, offset + i);
        if (length == 0) break;

        switch(tvb_get_uint8(tvb, offset + i + 1)) {
        case 0x08: /* Device Name, shortened */
        case 0x09: /* Device Name, full */
            name = tvb_get_string_enc(pinfo->pool, tvb, offset + i + 2, length - 1, ENC_ASCII);

            k_interface_id = bluetooth_data->interface_id;
            k_adapter_id = bluetooth_data->adapter_id;
            k_frame_number = pinfo->num;

            key[0].length = 1;
            key[0].key    = &k_interface_id;
            key[1].length = 1;
            key[1].key    = &k_adapter_id;
            key[2].length = 1;
            key[2].key    = &k_frame_number;
            key[3].length = 0;
            key[3].key    = NULL;

            localhost_name_entry = (localhost_name_entry_t *) wmem_new(wmem_file_scope(), localhost_name_entry_t);
            localhost_name_entry->interface_id = k_interface_id;
            localhost_name_entry->adapter_id = k_adapter_id;
            localhost_name_entry->name = wmem_strdup(wmem_file_scope(), name);

            wmem_tree_insert32_array(bluetooth_data->localhost_name, key, localhost_name_entry);

            break;
        }

        i += length + 1;
    }
}


static const char* bluetooth_conv_get_filter_type(conv_item_t* conv, conv_filter_type_e filter)
{
    if (filter == CONV_FT_SRC_ADDRESS) {
        if (conv->src_address.type == AT_ETHER)
            return "bluetooth.src";
        else if (conv->src_address.type == AT_STRINGZ)
            return "bluetooth.src_str";
    }

    if (filter == CONV_FT_DST_ADDRESS) {
        if (conv->dst_address.type == AT_ETHER)
            return "bluetooth.dst";
        else if (conv->dst_address.type == AT_STRINGZ)
            return "bluetooth.dst_str";
    }

    if (filter == CONV_FT_ANY_ADDRESS) {
        if (conv->src_address.type == AT_ETHER && conv->dst_address.type == AT_ETHER)
            return "bluetooth.addr";
        else if (conv->src_address.type == AT_STRINGZ && conv->dst_address.type == AT_STRINGZ)
            return "bluetooth.addr_str";
    }

    return CONV_FILTER_INVALID;
}

static ct_dissector_info_t bluetooth_ct_dissector_info = {&bluetooth_conv_get_filter_type};


static const char* bluetooth_endpoint_get_filter_type(endpoint_item_t* endpoint, conv_filter_type_e filter)
{
    if (filter == CONV_FT_ANY_ADDRESS) {
        if (endpoint->myaddress.type == AT_ETHER)
            return "bluetooth.addr";
        else if (endpoint->myaddress.type == AT_STRINGZ)
            return "bluetooth.addr_str";
    }

    return CONV_FILTER_INVALID;
}

static et_dissector_info_t  bluetooth_et_dissector_info = {&bluetooth_endpoint_get_filter_type};


static tap_packet_status
bluetooth_conversation_packet(void *pct, packet_info *pinfo,
        epan_dissect_t *edt _U_, const void *vip _U_, tap_flags_t flags)
{
    conv_hash_t *hash = (conv_hash_t*) pct;
    hash->flags = flags;
    add_conversation_table_data(hash, &pinfo->dl_src, &pinfo->dl_dst, 0, 0, 1,
            pinfo->fd->pkt_len, &pinfo->rel_ts, &pinfo->abs_ts,
            &bluetooth_ct_dissector_info, CONVERSATION_NONE);

    return TAP_PACKET_REDRAW;
}


static tap_packet_status
bluetooth_endpoint_packet(void *pit, packet_info *pinfo,
        epan_dissect_t *edt _U_, const void *vip _U_, tap_flags_t flags)
{
    conv_hash_t *hash = (conv_hash_t*) pit;
    hash->flags = flags;

    add_endpoint_table_data(hash, &pinfo->dl_src, 0, true,  1, pinfo->fd->pkt_len, &bluetooth_et_dissector_info, ENDPOINT_NONE);
    add_endpoint_table_data(hash, &pinfo->dl_dst, 0, false, 1, pinfo->fd->pkt_len, &bluetooth_et_dissector_info, ENDPOINT_NONE);

    return TAP_PACKET_REDRAW;
}

static conversation_t *
get_conversation(packet_info *pinfo,
                     address *src_addr, address *dst_addr,
                     uint32_t src_endpoint, uint32_t dst_endpoint)
{
    conversation_t *conversation;

    conversation = find_conversation(pinfo->num,
                               src_addr, dst_addr,
                               CONVERSATION_BLUETOOTH,
                               src_endpoint, dst_endpoint, 0);
    if (conversation) {
        return conversation;
    }

    conversation = conversation_new(pinfo->num,
                           src_addr, dst_addr,
                           CONVERSATION_BLUETOOTH,
                           src_endpoint, dst_endpoint, 0);
    return conversation;
}

static bluetooth_uuid_t
get_bluetooth_uuid_from_str(const char *str)
{
    bluetooth_uuid_t  uuid;
    char digits[3];
    const char *p = str;

    memset(&uuid, 0, sizeof(uuid));

    ws_return_val_if(!str, uuid);

    static const char fmt[] = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX";
    const size_t fmtchars = sizeof(fmt) - 1;

    size_t size = strlen(str);
    if (size != 4 && size != 8 && size != fmtchars) {
        return uuid;
    }

    for (size_t i = 0; i < size; i++) {
        if (fmt[i] == 'X') {
            if (!g_ascii_isxdigit(str[i]))
                return uuid;
        } else {
            if (str[i] != fmt[i])
                return uuid;
        }
    }

    if (size == 4) {
        size = 2;
    } else if (size == 8) {
        size = 4;
    } else if (size == fmtchars) {
        size = 16;
    } else {
        ws_assert_not_reached();
    }

    for (size_t i = 0; i < size; i++) {
        if (*p == '-') ++p;
        digits[0] = *(p++);
        digits[1] = *(p++);
        digits[2] = '\0';
        uuid.data[i] = (uint8_t)strtoul(digits, NULL, 16);
    }

    if (size == 4) {
        if (uuid.data[0] == 0x00 && uuid.data[1] == 0x00) {
            uuid.data[0] = uuid.data[2];
            uuid.data[1] = uuid.data[3];
            size = 2;
        }
    } else if (size == 16) {
        if (uuid.data[0] == 0x00 && uuid.data[1] == 0x00 &&
            uuid.data[4]  == 0x00 && uuid.data[5]  == 0x00 && uuid.data[6]  == 0x10 &&
            uuid.data[7]  == 0x00 && uuid.data[8]  == 0x80 && uuid.data[9]  == 0x00 &&
            uuid.data[10] == 0x00 && uuid.data[11] == 0x80 && uuid.data[12] == 0x5F &&
            uuid.data[13] == 0x9B && uuid.data[14] == 0x34 && uuid.data[15] == 0xFB) {

            uuid.data[0] = uuid.data[2];
            uuid.data[1] = uuid.data[3];
            size = 2;
        }
    }

    if (size == 2) {
        uuid.bt_uuid = uuid.data[1] | uuid.data[0] << 8;
    }
    uuid.size = (uint8_t)size;
    return uuid;
}

bluetooth_uuid_t
get_bluetooth_uuid(tvbuff_t *tvb, int offset, int size)
{
    bluetooth_uuid_t  uuid;

    memset(&uuid, 0, sizeof(uuid));

    if (size != 2 && size != 4 && size != 16) {
        return uuid;
    }

    if (size == 2) {
        uuid.data[0] = tvb_get_uint8(tvb, offset + 1);
        uuid.data[1] = tvb_get_uint8(tvb, offset);

        uuid.bt_uuid = uuid.data[1] | uuid.data[0] << 8;
    } else if (size == 4) {
        uuid.data[0] = tvb_get_uint8(tvb, offset + 3);
        uuid.data[1] = tvb_get_uint8(tvb, offset + 2);
        uuid.data[2] = tvb_get_uint8(tvb, offset + 1);
        uuid.data[3] = tvb_get_uint8(tvb, offset);

        if (uuid.data[0] == 0x00 && uuid.data[1] == 0x00) {
            uuid.bt_uuid = uuid.data[3] | uuid.data[2] << 8;
            size = 2;
        }
    } else {
        uuid.data[0] = tvb_get_uint8(tvb, offset + 15);
        uuid.data[1] = tvb_get_uint8(tvb, offset + 14);
        uuid.data[2] = tvb_get_uint8(tvb, offset + 13);
        uuid.data[3] = tvb_get_uint8(tvb, offset + 12);
        uuid.data[4] = tvb_get_uint8(tvb, offset + 11);
        uuid.data[5] = tvb_get_uint8(tvb, offset + 10);
        uuid.data[6] = tvb_get_uint8(tvb, offset + 9);
        uuid.data[7] = tvb_get_uint8(tvb, offset + 8);
        uuid.data[8] = tvb_get_uint8(tvb, offset + 7);
        uuid.data[9] = tvb_get_uint8(tvb, offset + 6);
        uuid.data[10] = tvb_get_uint8(tvb, offset + 5);
        uuid.data[11] = tvb_get_uint8(tvb, offset + 4);
        uuid.data[12] = tvb_get_uint8(tvb, offset + 3);
        uuid.data[13] = tvb_get_uint8(tvb, offset + 2);
        uuid.data[14] = tvb_get_uint8(tvb, offset + 1);
        uuid.data[15] = tvb_get_uint8(tvb, offset);

        if (uuid.data[0] == 0x00 && uuid.data[1] == 0x00 &&
            uuid.data[4]  == 0x00 && uuid.data[5]  == 0x00 && uuid.data[6]  == 0x10 &&
            uuid.data[7]  == 0x00 && uuid.data[8]  == 0x80 && uuid.data[9]  == 0x00 &&
            uuid.data[10] == 0x00 && uuid.data[11] == 0x80 && uuid.data[12] == 0x5F &&
            uuid.data[13] == 0x9B && uuid.data[14] == 0x34 && uuid.data[15] == 0xFB) {
            uuid.bt_uuid = uuid.data[3] | uuid.data[2] << 8;
            size = 2;
        }
    }

    uuid.size = size;
    return uuid;
}

const char *
print_numeric_bluetooth_uuid(wmem_allocator_t *pool, const bluetooth_uuid_t *uuid)
{
    if (!(uuid && uuid->size > 0))
        return NULL;

    if (uuid->size != 16) {
        /* XXX - This is not right for UUIDs that were 32 or 128-bit in a
         * tvb and converted to 16-bit UUIDs by get_bluetooth_uuid.
         */
        return bytes_to_str(pool, uuid->data, uuid->size);
    } else {
        char *text;

        text = (char *) wmem_alloc(pool, 38);
        bytes_to_hexstr(&text[0], uuid->data, 4);
        text[8] = '-';
        bytes_to_hexstr(&text[9], uuid->data + 4, 2);
        text[13] = '-';
        bytes_to_hexstr(&text[14], uuid->data + 4 + 2 * 1, 2);
        text[18] = '-';
        bytes_to_hexstr(&text[19], uuid->data + 4 + 2 * 2, 2);
        text[23] = '-';
        bytes_to_hexstr(&text[24], uuid->data + 4 + 2 * 3, 6);
        text[36] = '\0';

        return text;
    }

    return NULL;
}

const char *
print_bluetooth_uuid(wmem_allocator_t *pool _U_, const bluetooth_uuid_t *uuid)
{
    const char *description;

    if (uuid->bt_uuid) {
        const char *name;

        /*
         * Known UUID?
         */
        name = try_val_to_str_ext(uuid->bt_uuid, &bluetooth_uuid_vals_ext);
        if (name != NULL) {
            /*
             * Yes.  This string is part of the value_string_ext table,
             * so we don't have to make a copy.
             */
            return name;
        }

        /*
         * No - fall through to try looking it up.
         */
    }

    description = bluetooth_get_custom_uuid_description(uuid);
    if (description)
        return description;

    return "Unknown";
}

bluetooth_data_t *
dissect_bluetooth_common(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
    proto_item        *main_item;
    proto_tree        *main_tree;
    proto_item        *sub_item;
    bluetooth_data_t  *bluetooth_data;
    address           *src;
    address           *dst;

    col_set_str(pinfo->cinfo, COL_PROTOCOL, "Bluetooth");
    switch (pinfo->p2p_dir) {

    case P2P_DIR_SENT:
        col_set_str(pinfo->cinfo, COL_INFO, "Sent ");
        break;

    case P2P_DIR_RECV:
        col_set_str(pinfo->cinfo, COL_INFO, "Rcvd ");
        break;

    default:
        col_set_str(pinfo->cinfo, COL_INFO, "UnknownDirection ");
        break;
    }

    pinfo->ptype = PT_BLUETOOTH;
    get_conversation(pinfo, &pinfo->dl_src, &pinfo->dl_dst, pinfo->srcport, pinfo->destport);

    main_item = proto_tree_add_item(tree, proto_bluetooth, tvb, 0, tvb_captured_length(tvb), ENC_NA);
    main_tree = proto_item_add_subtree(main_item, ett_bluetooth);

    bluetooth_data = (bluetooth_data_t *) wmem_new(pinfo->pool, bluetooth_data_t);
    if (pinfo->rec->presence_flags & WTAP_HAS_INTERFACE_ID)
        bluetooth_data->interface_id = pinfo->rec->rec_header.packet_header.interface_id;
    else
        bluetooth_data->interface_id = HCI_INTERFACE_DEFAULT;
    bluetooth_data->adapter_id = HCI_ADAPTER_DEFAULT;
    bluetooth_data->adapter_disconnect_in_frame  = &bluetooth_max_disconnect_in_frame;
    bluetooth_data->chandle_sessions             = chandle_sessions;
    bluetooth_data->chandle_to_bdaddr            = chandle_to_bdaddr;
    bluetooth_data->chandle_to_mode              = chandle_to_mode;
    bluetooth_data->shandle_to_chandle           = shandle_to_chandle;
    bluetooth_data->bdaddr_to_name               = bdaddr_to_name;
    bluetooth_data->bdaddr_to_role               = bdaddr_to_role;
    bluetooth_data->localhost_bdaddr             = localhost_bdaddr;
    bluetooth_data->localhost_name               = localhost_name;
    bluetooth_data->hci_vendors                  = hci_vendors;
    bluetooth_data->cs_configurations            = cs_configurations;

    if (have_tap_listener(bluetooth_tap)) {
        bluetooth_tap_data_t  *bluetooth_tap_data;

        bluetooth_tap_data                = wmem_new(pinfo->pool, bluetooth_tap_data_t);
        bluetooth_tap_data->interface_id  = bluetooth_data->interface_id;
        bluetooth_tap_data->adapter_id    = bluetooth_data->adapter_id;

        tap_queue_packet(bluetooth_tap, pinfo, bluetooth_tap_data);
    }

    src = (address *) p_get_proto_data(wmem_file_scope(), pinfo, proto_bluetooth, BLUETOOTH_DATA_SRC);
    dst = (address *) p_get_proto_data(wmem_file_scope(), pinfo, proto_bluetooth, BLUETOOTH_DATA_DST);

    if (src && src->type == AT_STRINGZ) {
        sub_item = proto_tree_add_string(main_tree, hf_bluetooth_addr_str, tvb, 0, 0, (const char *) src->data);
        proto_item_set_hidden(sub_item);

        sub_item = proto_tree_add_string(main_tree, hf_bluetooth_src_str, tvb, 0, 0, (const char *) src->data);
        proto_item_set_generated(sub_item);
    } else if (src && src->type == AT_ETHER) {
        sub_item = proto_tree_add_ether(main_tree, hf_bluetooth_addr, tvb, 0, 0, (const uint8_t *) src->data);
        proto_item_set_hidden(sub_item);

        sub_item = proto_tree_add_ether(main_tree, hf_bluetooth_src, tvb, 0, 0, (const uint8_t *) src->data);
        proto_item_set_generated(sub_item);
    }

    if (dst && dst->type == AT_STRINGZ) {
        sub_item = proto_tree_add_string(main_tree, hf_bluetooth_addr_str, tvb, 0, 0, (const char *) dst->data);
        proto_item_set_hidden(sub_item);

        sub_item = proto_tree_add_string(main_tree, hf_bluetooth_dst_str, tvb, 0, 0, (const char *) dst->data);
        proto_item_set_generated(sub_item);
    } else if (dst && dst->type == AT_ETHER) {
        sub_item = proto_tree_add_ether(main_tree, hf_bluetooth_addr, tvb, 0, 0, (const uint8_t *) dst->data);
        proto_item_set_hidden(sub_item);

        sub_item = proto_tree_add_ether(main_tree, hf_bluetooth_dst, tvb, 0, 0, (const uint8_t *) dst->data);
        proto_item_set_generated(sub_item);
    }

    return bluetooth_data;
}

/*
 * Register this in the wtap_encap dissector table.
 * It's called for WTAP_ENCAP_BLUETOOTH_H4, WTAP_ENCAP_BLUETOOTH_H4_WITH_PHDR,
 * WTAP_ENCAP_PACKETLOGGER. WTAP_ENCAP_BLUETOOTH_LE_LL,
 * WTAP_ENCAP_BLUETOOTH_LE_LL_WITH_PHDR, and WTAP_ENCAP_BLUETOOTH_BREDR_BB.
 *
 * It does work common to all Bluetooth encapsulations, and then calls
 * the dissector registered in the bluetooth.encap table to handle the
 * metadata header in the packet.
 */
static int
dissect_bluetooth(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data _U_)
{
    bluetooth_data_t  *bluetooth_data;

    bluetooth_data = dissect_bluetooth_common(tvb, pinfo, tree);

    /*
     * There is no pseudo-header, or there's just a p2p pseudo-header.
     */
    bluetooth_data->previous_protocol_data_type = BT_PD_NONE;
    bluetooth_data->previous_protocol_data.none = NULL;

    if (!dissector_try_uint_with_data(bluetooth_table, pinfo->rec->rec_header.packet_header.pkt_encap, tvb, pinfo, tree, true, bluetooth_data)) {
        call_data_dissector(tvb, pinfo, tree);
    }

    return tvb_captured_length(tvb);
}


/*
 * Register this in the wtap_encap dissector table.
 * It's called for WTAP_ENCAP_BLUETOOTH_HCI.
 *
 * It does work common to all Bluetooth encapsulations, and then calls
 * the dissector registered in the bluetooth.encap table to handle the
 * metadata header in the packet.
 */
static int
dissect_bluetooth_bthci(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data)
{
    bluetooth_data_t  *bluetooth_data;

    bluetooth_data = dissect_bluetooth_common(tvb, pinfo, tree);

    /*
     * data points to a struct bthci_phdr.
     */
    bluetooth_data->previous_protocol_data_type = BT_PD_BTHCI;
    bluetooth_data->previous_protocol_data.bthci = (struct bthci_phdr *)data;

    if (!dissector_try_uint_with_data(bluetooth_table, pinfo->rec->rec_header.packet_header.pkt_encap, tvb, pinfo, tree, true, bluetooth_data)) {
        call_data_dissector(tvb, pinfo, tree);
    }

    return tvb_captured_length(tvb);
}

/*
 * Register this in the wtap_encap dissector table.
 * It's called for WTAP_ENCAP_BLUETOOTH_LINUX_MONITOR.
 *
 * It does work common to all Bluetooth encapsulations, and then calls
 * the dissector registered in the bluetooth.encap table to handle the
 * metadata header in the packet.
 */
static int
dissect_bluetooth_btmon(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data)
{
    bluetooth_data_t  *bluetooth_data;

    bluetooth_data = dissect_bluetooth_common(tvb, pinfo, tree);

    /*
     * data points to a struct btmon_phdr.
     */
    bluetooth_data->previous_protocol_data_type = BT_PD_BTMON;
    bluetooth_data->previous_protocol_data.btmon = (struct btmon_phdr *)data;

    if (!dissector_try_uint_with_data(bluetooth_table, pinfo->rec->rec_header.packet_header.pkt_encap, tvb, pinfo, tree, true, bluetooth_data)) {
        call_data_dissector(tvb, pinfo, tree);
    }

    return tvb_captured_length(tvb);
}

/*
 * Register this in various USB dissector tables.
 */
static int
dissect_bluetooth_usb(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data)
{
    bluetooth_data_t  *bluetooth_data;

    bluetooth_data = dissect_bluetooth_common(tvb, pinfo, tree);

    /*
     * data points to a urb_info_t.
     */
    bluetooth_data->previous_protocol_data_type = BT_PD_URB_INFO;
    bluetooth_data->previous_protocol_data.urb = (urb_info_t *)data;

    return call_dissector_with_data(hci_usb_handle, tvb, pinfo, tree, bluetooth_data);
}

/*
 * Register this by name; it's called from the Ubertooth dissector.
 */
static int
dissect_bluetooth_ubertooth(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data)
{
    bluetooth_data_t  *bluetooth_data;

    bluetooth_data = dissect_bluetooth_common(tvb, pinfo, tree);

    /*
     * data points to a ubertooth_data_t.
     */
    bluetooth_data->previous_protocol_data_type = BT_PD_UBERTOOTH_DATA;
    bluetooth_data->previous_protocol_data.ubertooth_data = (ubertooth_data_t *)data;

    call_dissector(btle_handle, tvb, pinfo, tree);

    return tvb_captured_length(tvb);
}

void
proto_register_bluetooth(void)
{
    static hf_register_info hf[] = {
        { &hf_bluetooth_src,
            { "Source",                              "bluetooth.src",
            FT_ETHER, BASE_NONE, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_bluetooth_dst,
            { "Destination",                         "bluetooth.dst",
            FT_ETHER, BASE_NONE, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_bluetooth_addr,
            { "Source or Destination",               "bluetooth.addr",
            FT_ETHER, BASE_NONE, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_bluetooth_src_str,
            { "Source",                              "bluetooth.src_str",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_bluetooth_dst_str,
            { "Destination",                         "bluetooth.dst_str",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_bluetooth_addr_str,
            { "Source or Destination",               "bluetooth.addr_str",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL }
        },
    };

    static hf_register_info oui_hf[] = {
        { &hf_llc_bluetooth_pid,
            { "PID",    "llc.bluetooth_pid",
            FT_UINT16, BASE_HEX, VALS(bluetooth_pid_vals), 0x0,
            "Protocol ID", HFILL }
        }
    };

    static int *ett[] = {
        &ett_bluetooth,
    };

    // UAT
    module_t *bluetooth_module;
    uat_t* bluetooth_uuids_uat;
    static uat_field_t bluetooth_uuids_uat_fields[] = {
        UAT_FLD_CSTRING(bt_uuids, uuid, "UUID", "UUID"),
        UAT_FLD_CSTRING(bt_uuids, label, "UUID Name", "Readable label"),
        UAT_FLD_BOOL(bt_uuids, long_attr, "Long Attribute", "A Long Attribute that may be sent in multiple BT ATT PDUs"),
        UAT_END_FIELDS
    };

    /* Decode As handling */
    static build_valid_func bluetooth_uuid_da_build_value[1] = {bluetooth_uuid_value};
    static decode_as_value_t bluetooth_uuid_da_values = {bluetooth_uuid_prompt, 1, bluetooth_uuid_da_build_value};
    static decode_as_t bluetooth_uuid_da = {"bluetooth", "bluetooth.uuid", 1, 0, &bluetooth_uuid_da_values, NULL, NULL,
            decode_as_default_populate_list, decode_as_default_reset, decode_as_default_change, NULL};


    proto_bluetooth = proto_register_protocol("Bluetooth", "Bluetooth", "bluetooth");
    prefs_register_protocol(proto_bluetooth, NULL);

    register_dissector("bluetooth_ubertooth", dissect_bluetooth_ubertooth, proto_bluetooth);

    proto_register_field_array(proto_bluetooth, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));

    bluetooth_table = register_dissector_table("bluetooth.encap",
            "Bluetooth Encapsulation", proto_bluetooth, FT_UINT32, BASE_HEX);

    chandle_sessions         = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    chandle_to_bdaddr        = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    chandle_to_mode          = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    shandle_to_chandle       = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    bdaddr_to_name           = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    bdaddr_to_role           = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    localhost_bdaddr         = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    localhost_name           = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    hci_vendors              = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());
    cs_configurations        = wmem_tree_new_autoreset(wmem_epan_scope(), wmem_file_scope());

    hci_vendor_table = register_dissector_table("bluetooth.vendor", "HCI Vendor", proto_bluetooth, FT_UINT16, BASE_HEX);
    bluetooth_uuids          = wmem_tree_new(wmem_epan_scope());

    bluetooth_tap = register_tap("bluetooth");
    bluetooth_device_tap = register_tap("bluetooth.device");
    bluetooth_hci_summary_tap = register_tap("bluetooth.hci_summary");

    bluetooth_uuid_table = register_dissector_table("bluetooth.uuid", "BT Service UUID", proto_bluetooth, FT_STRING, STRING_CASE_SENSITIVE);
    llc_add_oui(OUI_BLUETOOTH, "llc.bluetooth_pid", "LLC Bluetooth OUI PID", oui_hf, proto_bluetooth);

    register_conversation_table(proto_bluetooth, true, bluetooth_conversation_packet, bluetooth_endpoint_packet);

    register_decode_as(&bluetooth_uuid_da);

    bluetooth_module = prefs_register_protocol(proto_bluetooth, NULL);
    bluetooth_uuids_uat = uat_new("Custom Bluetooth UUIDs",
                                  sizeof(bt_uuid_t),
                                  "bluetooth_uuids",
                                  true,
                                  &bt_uuids,
                                  &num_bt_uuids,
                                  UAT_AFFECTS_DISSECTION,
                                  NULL,
                                  bt_uuids_copy_cb,
                                  bt_uuids_update_cb,
                                  bt_uuids_free_cb,
                                  bt_uuids_post_update_cb,
                                  bt_uuids_reset_cb,
                                  bluetooth_uuids_uat_fields);

    static const char* bt_uuids_uat_defaults_[] = {
      NULL, NULL, "FALSE" };
    uat_set_default_values(bluetooth_uuids_uat, bt_uuids_uat_defaults_);

    prefs_register_uat_preference(bluetooth_module, "uuids",
                                  "Custom Bluetooth UUID names",
                                  "Assign readable names to custom UUIDs",
                                  bluetooth_uuids_uat);

    bluetooth_handle = register_dissector("bluetooth", dissect_bluetooth, proto_bluetooth);
    bluetooth_bthci_handle = register_dissector("bluetooth.bthci", dissect_bluetooth_bthci, proto_bluetooth);
    bluetooth_btmon_handle = register_dissector("bluetooth.btmon", dissect_bluetooth_btmon, proto_bluetooth);
    bluetooth_usb_handle = register_dissector("bluetooth.usb", dissect_bluetooth_usb, proto_bluetooth);
}

void
proto_reg_handoff_bluetooth(void)
{
    dissector_handle_t eapol_handle;
    dissector_handle_t btl2cap_handle;

    btle_handle = find_dissector_add_dependency("btle", proto_bluetooth);
    hci_usb_handle = find_dissector_add_dependency("hci_usb", proto_bluetooth);

    dissector_add_uint("wtap_encap", WTAP_ENCAP_BLUETOOTH_HCI,           bluetooth_bthci_handle);
    dissector_add_uint("wtap_encap", WTAP_ENCAP_BLUETOOTH_H4,            bluetooth_handle);
    dissector_add_uint("wtap_encap", WTAP_ENCAP_BLUETOOTH_H4_WITH_PHDR,  bluetooth_handle);
    dissector_add_uint("wtap_encap", WTAP_ENCAP_BLUETOOTH_LINUX_MONITOR, bluetooth_btmon_handle);
    dissector_add_uint("wtap_encap", WTAP_ENCAP_PACKETLOGGER,            bluetooth_handle);

    dissector_add_uint("wtap_encap", WTAP_ENCAP_BLUETOOTH_LE_LL,           bluetooth_handle);
    dissector_add_uint("wtap_encap", WTAP_ENCAP_BLUETOOTH_LE_LL_WITH_PHDR, bluetooth_handle);
    dissector_add_uint("wtap_encap", WTAP_ENCAP_BLUETOOTH_BREDR_BB,        bluetooth_handle);

    dissector_add_uint("usb.product", (0x0a5c << 16) | 0x21e8, bluetooth_usb_handle);
    dissector_add_uint("usb.product", (0x1131 << 16) | 0x1001, bluetooth_usb_handle);
    dissector_add_uint("usb.product", (0x050d << 16) | 0x0081, bluetooth_usb_handle);
    dissector_add_uint("usb.product", (0x0a5c << 16) | 0x2198, bluetooth_usb_handle);
    dissector_add_uint("usb.product", (0x0a5c << 16) | 0x21e8, bluetooth_usb_handle);
    dissector_add_uint("usb.product", (0x04bf << 16) | 0x0320, bluetooth_usb_handle);
    dissector_add_uint("usb.product", (0x13d3 << 16) | 0x3375, bluetooth_usb_handle);

    dissector_add_uint("usb.protocol", 0xE00101, bluetooth_usb_handle);
    dissector_add_uint("usb.protocol", 0xE00104, bluetooth_usb_handle);

    dissector_add_for_decode_as("usb.device", bluetooth_usb_handle);

    bluetooth_add_custom_uuid("00000001-0000-1000-8000-0002EE000002", "SyncML Server", false);
    bluetooth_add_custom_uuid("00000002-0000-1000-8000-0002EE000002", "SyncML Client", false);
    bluetooth_add_custom_uuid("7905F431-B5CE-4E99-A40F-4B1E122D00D0", "Apple Notification Center Service", false);

    eapol_handle = find_dissector("eapol");
    btl2cap_handle = find_dissector("btl2cap");

    dissector_add_uint("llc.bluetooth_pid", AMP_C_SECURITY_FRAME, eapol_handle);
    dissector_add_uint("llc.bluetooth_pid", AMP_U_L2CAP, btl2cap_handle);

/* TODO: Add UUID128 version of UUID16; UUID32? UUID16? */
}

static int proto_btad_apple_ibeacon;

static int hf_btad_apple_ibeacon_type;
static int hf_btad_apple_ibeacon_length;
static int hf_btad_apple_ibeacon_uuid128;
static int hf_btad_apple_ibeacon_major;
static int hf_btad_apple_ibeacon_minor;
static int hf_btad_apple_ibeacon_measured_power;

static int ett_btad_apple_ibeacon;

static dissector_handle_t btad_apple_ibeacon;

void proto_register_btad_apple_ibeacon(void);
void proto_reg_handoff_btad_apple_ibeacon(void);


static int
dissect_btad_apple_ibeacon(tvbuff_t *tvb, packet_info *pinfo _U_, proto_tree *tree, void *data _U_)
{
    proto_tree       *main_tree;
    proto_item       *main_item;
    int               offset = 0;

    main_item = proto_tree_add_item(tree, proto_btad_apple_ibeacon, tvb, offset, tvb_captured_length(tvb), ENC_NA);
    main_tree = proto_item_add_subtree(main_item, ett_btad_apple_ibeacon);

    proto_tree_add_item(main_tree, hf_btad_apple_ibeacon_type, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(main_tree, hf_btad_apple_ibeacon_length, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(main_tree, hf_btad_apple_ibeacon_uuid128, tvb, offset, 16, ENC_BIG_ENDIAN);
    offset += 16;

    proto_tree_add_item(main_tree, hf_btad_apple_ibeacon_major, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    proto_tree_add_item(main_tree, hf_btad_apple_ibeacon_minor, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    proto_tree_add_item(main_tree, hf_btad_apple_ibeacon_measured_power, tvb, offset, 1, ENC_NA);
    offset += 1;

    return offset;
}

void
proto_register_btad_apple_ibeacon(void)
{
    static hf_register_info hf[] = {
        {&hf_btad_apple_ibeacon_type,
            {"Type",                             "bluetooth.apple.ibeacon.type",
            FT_UINT8, BASE_DEC, NULL, 0x0,
            NULL, HFILL}
        },
        {&hf_btad_apple_ibeacon_length,
            {"Length",                           "bluetooth.apple.ibeacon.length",
            FT_UINT8, BASE_DEC, NULL, 0x0,
            NULL, HFILL}
        },
        {&hf_btad_apple_ibeacon_uuid128,
            {"UUID",                             "bluetooth.apple.ibeacon.uuid128",
            FT_GUID, BASE_NONE, NULL, 0x0,
            NULL, HFILL}
        },
        { &hf_btad_apple_ibeacon_major,
          { "Major",                             "bluetooth.apple.ibeacon.major",
            FT_UINT16, BASE_DEC, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_btad_apple_ibeacon_minor,
          { "Minor",                             "bluetooth.apple.ibeacon.minor",
            FT_UINT16, BASE_DEC, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_btad_apple_ibeacon_measured_power,
          { "Measured Power",                    "bluetooth.apple.ibeacon.measured_power",
            FT_INT8, BASE_DEC|BASE_UNIT_STRING, UNS(&units_dbm), 0x0,
            NULL, HFILL }
        }
    };

    static int *ett[] = {
        &ett_btad_apple_ibeacon,
    };

    proto_btad_apple_ibeacon = proto_register_protocol("Apple iBeacon", "iBeacon", "ibeacon");
    proto_register_field_array(proto_btad_apple_ibeacon, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
    btad_apple_ibeacon = register_dissector("bluetooth.apple.ibeacon", dissect_btad_apple_ibeacon, proto_btad_apple_ibeacon);
}


void
proto_reg_handoff_btad_apple_ibeacon(void)
{
    dissector_add_for_decode_as("btcommon.eir_ad.manufacturer_company_id", btad_apple_ibeacon);
}


static int proto_btad_alt_beacon;

static int hf_btad_alt_beacon_code;
static int hf_btad_alt_beacon_id;
static int hf_btad_alt_beacon_reference_rssi;
static int hf_btad_alt_beacon_manufacturer_data;

static int ett_btad_alt_beacon;

static dissector_handle_t btad_alt_beacon;

void proto_register_btad_alt_beacon(void);
void proto_reg_handoff_btad_alt_beacon(void);


static int
dissect_btad_alt_beacon(tvbuff_t *tvb, packet_info *pinfo _U_, proto_tree *tree, void *data _U_)
{
    proto_tree       *main_tree;
    proto_item       *main_item;
    int               offset = 0;

    main_item = proto_tree_add_item(tree, proto_btad_alt_beacon, tvb, offset, tvb_captured_length(tvb), ENC_NA);
    main_tree = proto_item_add_subtree(main_item, ett_btad_alt_beacon);

    proto_tree_add_item(main_tree, hf_btad_alt_beacon_code, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    proto_tree_add_item(main_tree, hf_btad_alt_beacon_id, tvb, offset, 20, ENC_NA);
    offset += 20;

    proto_tree_add_item(main_tree, hf_btad_alt_beacon_reference_rssi, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(main_tree, hf_btad_alt_beacon_manufacturer_data, tvb, offset, 1, ENC_NA);
    offset += 1;

    return offset;
}

void
proto_register_btad_alt_beacon(void)
{
    static hf_register_info hf[] = {
        { &hf_btad_alt_beacon_code,
          { "Code",                              "bluetooth.alt_beacon.code",
            FT_UINT16, BASE_HEX, NULL, 0x0,
            NULL, HFILL }
        },
        {&hf_btad_alt_beacon_id,
            {"ID",                               "bluetooth.alt_beacon.id",
            FT_BYTES, BASE_NONE, NULL, 0x0,
            NULL, HFILL}
        },
        { &hf_btad_alt_beacon_reference_rssi,
          { "Reference RSSI",                    "bluetooth.alt_beacon.reference_rssi",
            FT_INT8, BASE_DEC, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_btad_alt_beacon_manufacturer_data,
          { "Manufacturer Data",                 "bluetooth.alt_beacon.manufacturer_data",
            FT_UINT8, BASE_HEX, NULL, 0x0,
            NULL, HFILL }
        }
    };

    static int *ett[] = {
        &ett_btad_alt_beacon,
    };

    proto_btad_alt_beacon = proto_register_protocol("AltBeacon", "AltBeacon", "alt_beacon");
    proto_register_field_array(proto_btad_alt_beacon, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
    btad_alt_beacon = register_dissector("bluetooth.alt_beacon", dissect_btad_alt_beacon, proto_btad_alt_beacon);
}

void
proto_reg_handoff_btad_alt_beacon(void)
{
    dissector_add_for_decode_as("btcommon.eir_ad.manufacturer_company_id", btad_alt_beacon);
}

static int proto_btad_gaen;

static int hf_btad_gaen_rpi128;
static int hf_btad_gaen_aemd32;

static int ett_btad_gaen;

static dissector_handle_t btad_gaen;

void proto_register_btad_gaen(void);
void proto_reg_handoff_btad_gaen(void);

static int
dissect_btad_gaen(tvbuff_t *tvb, packet_info *pinfo _U_, proto_tree *tree, void *data _U_)
{
    proto_tree       *main_tree;
    proto_item       *main_item;
    int              offset = 0;

    /* The "Service Data" blob of data has the following format for GAEN:
    1 byte: length (0x17)
    1 byte: Type (0x16)
    2 bytes: Identifier (should be 0xFD6F again)
    16 bytes: Rolling Proximity Identifier
    4 bytes: Associated Encrypted Metadata (Encrypted in AES-CTR mode)
    1 byte: Version
    1 byte: Power level
    2 bytes: Reserved for future use.

    We want to skip everything before the last 20 bytes, because it'll be handled by other parts of the BTLE dissector. */
    offset = tvb_captured_length(tvb) - 20;

    main_item = proto_tree_add_item(tree, proto_btad_gaen, tvb, offset, -1, ENC_NA);
    main_tree = proto_item_add_subtree(main_item, ett_btad_gaen);

    proto_tree_add_item(main_tree, hf_btad_gaen_rpi128, tvb, offset, 16, ENC_NA);
    offset += 16;

    proto_tree_add_item(main_tree, hf_btad_gaen_aemd32, tvb, offset, 4, ENC_NA);
    offset += 4;

    return offset;
}

void
proto_register_btad_gaen(void)
{
    static hf_register_info hf[] = {
        { &hf_btad_gaen_rpi128,
    { "Rolling Proximity Identifier",    "bluetooth.gaen.rpi",
    FT_BYTES, BASE_NONE, NULL, 0x0,
    NULL, HFILL }
        },
    { &hf_btad_gaen_aemd32,
    { "Associated Encrypted Metadata",   "bluetooth.gaen.aemd",
        FT_BYTES, BASE_NONE, NULL, 0x0,
        NULL, HFILL }
    }
    };

    static int *ett[] = {
        &ett_btad_gaen,
    };

    proto_btad_gaen = proto_register_protocol("Google/Apple Exposure Notification", "Google/Apple Exposure Notification", "bluetooth.gaen");
    proto_register_field_array(proto_btad_gaen, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
    btad_gaen = register_dissector("bluetooth.gaen", dissect_btad_gaen, proto_btad_gaen);
}

void
proto_reg_handoff_btad_gaen(void)
{
    dissector_add_string("btcommon.eir_ad.entry.uuid", "fd6f", btad_gaen);
}

static int proto_btad_matter;

static int hf_btad_matter_opcode;
static int hf_btad_matter_version;
static int hf_btad_matter_discriminator;
static int hf_btad_matter_vendor_id;
static int hf_btad_matter_product_id;
static int hf_btad_matter_flags;
static int hf_btad_matter_flags_additional_data;
static int hf_btad_matter_flags_ext_announcement;

static int ett_btad_matter;
static int ett_btad_matter_flags;

static dissector_handle_t btad_matter;

void proto_register_btad_matter(void);
void proto_reg_handoff_btad_matter(void);

static int
dissect_btad_matter(tvbuff_t *tvb, packet_info *pinfo _U_, proto_tree *tree, void *data _U_)
{
    /* We are interested only in the last 8 bytes (Service Data Payload) */
    int offset = tvb_captured_length(tvb) - 8;

    proto_tree *main_item = proto_tree_add_item(tree, proto_btad_matter, tvb, offset, -1, ENC_NA);
    proto_tree *main_tree = proto_item_add_subtree(main_item, ett_btad_matter);

    proto_tree_add_item(main_tree, hf_btad_matter_opcode, tvb, offset, 1, ENC_NA);
    offset += 1;

    proto_tree_add_item(main_tree, hf_btad_matter_version, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    proto_tree_add_item(main_tree, hf_btad_matter_discriminator, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;

    proto_tree_add_item(main_tree, hf_btad_matter_vendor_id, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;

    proto_tree_add_item(main_tree, hf_btad_matter_product_id, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;

    static int * const flags[] = {
        &hf_btad_matter_flags_additional_data,
        &hf_btad_matter_flags_ext_announcement,
        NULL
    };

    proto_tree_add_bitmask(main_tree, tvb, offset, hf_btad_matter_flags, ett_btad_matter_flags, flags, ENC_NA);
    offset += 1;

    return offset;
}

void
proto_register_btad_matter(void)
{
    static const value_string opcode_vals[] = {
        { 0x00, "Commissionable" },
        { 0, NULL }
    };

    static hf_register_info hf[] = {
        { &hf_btad_matter_opcode,
          { "Opcode", "bluetooth.matter.opcode",
            FT_UINT8, BASE_HEX, VALS(opcode_vals), 0x0,
            NULL, HFILL }
        },
        {&hf_btad_matter_version,
          {"Advertisement Version", "bluetooth.matter.version",
            FT_UINT16, BASE_DEC, NULL, 0xF000,
            NULL, HFILL}
        },
        { &hf_btad_matter_discriminator,
          { "Discriminator", "bluetooth.matter.discriminator",
            FT_UINT16, BASE_HEX, NULL, 0x0FFF,
            "A 12-bit value used in the Setup Code", HFILL }
        },
        { &hf_btad_matter_vendor_id,
          { "Vendor ID", "bluetooth.matter.vendor_id",
            FT_UINT16, BASE_HEX, NULL, 0x0,
            "A 16-bit value identifying the device manufacturer", HFILL }
        },
        { &hf_btad_matter_product_id,
          { "Product ID", "bluetooth.matter.product_id",
            FT_UINT16, BASE_HEX, NULL, 0x0,
            "A 16-bit value identifying the product", HFILL }
        },
        { &hf_btad_matter_flags,
          { "Flags", "bluetooth.matter.flags",
            FT_UINT8, BASE_HEX, NULL, 0x0,
            NULL, HFILL }
        },
        { &hf_btad_matter_flags_additional_data,
          { "Additional Data", "bluetooth.matter.flags.additional_data",
            FT_BOOLEAN, 8, NULL, 0x01,
            "Set if the device provides the optional C3 GATT characteristic", HFILL }
        },
        { &hf_btad_matter_flags_ext_announcement,
          { "Extended Announcement", "bluetooth.matter.flags.ext_announcement",
            FT_BOOLEAN, 8, NULL, 0x02,
            "Set while the device is in the Extended Announcement period", HFILL }
        },
    };

    static int *ett[] = {
        &ett_btad_matter,
        &ett_btad_matter_flags,
    };

    proto_btad_matter = proto_register_protocol("Matter Advertising Data", "Matter Advertising Data", "bluetooth.matter");
    proto_register_field_array(proto_btad_matter, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
    btad_matter = register_dissector("bluetooth.matter", dissect_btad_matter, proto_btad_matter);
}

void
proto_reg_handoff_btad_matter(void)
{
    dissector_add_string("btcommon.eir_ad.entry.uuid", "fff6", btad_matter);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
