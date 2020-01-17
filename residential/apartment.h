/* 	File: apartment.h

	Copyright (C) 2020, Regents of the Leland Stanford Junior University
 */

#ifndef _G_APARTMENT_H
#define _G_APARTMENT_H

#include "residential.h"

// core configuration
#define CC_NONE 0x0000
#define CC_INDOOR 0x0001
#define CC_DOUBLE 0x0003
#define CC_CONDITIONED 0x0005

// parking configuration
#define PC_OUTDOOR 0x0000
#define PC_INDOOR 0x0001

// system type central
#define STC_NONE 0x0000
#define STC_HEAT 0x0001
#define STC_COOL 0x0002
#define STC_BOTH 0x0003

// system type economizer
#define STE_NONE 0x0000
#define STE_DRYBULB 0x0001 // single temperature measurement only
#define STE_WETBULB 0x0002 // single temperature and humidity measurement
#define STE_DIFFERENTIAL 0x0003 // dual temperature and humidity measurement

// system plant mode
#define SPM_OFF 0x0000
#define SPM_VENTILATING 0x0001
#define SPM_HEATING 0x0002
#define SPM_COOLING 0x0003

// system type ventilation
#define STV_NONE 0x0000
#define STV_LOCAL 0x0001
#define STV_CENTRAL 0x0002

// unit appliance types
#define UAT_NONE         (0x0000)
#define UAT_COOKING      (0x0001)
#define UAT_DISHWASHER   (0x0002)
#define UAT_DRYER        (0x0004)
#define UAT_REFRIGERATOR (0x0008)
#define UAT_WASHER       (0x0010)
#define UAT_ALL          (0x001f)

// unit system type
#define UST_NONE 0x0000
#define UST_HEAT 0x0001
#define UST_COOL 0x0002
#define UST_BOTH 0x0003

class apartment : public gld_object {

public:
	typedef set CORECONFIGURATION;
	typedef enumeration PARKINGCONFIGURATION;
	typedef set SYSTEMTYPECENTRAL;
	typedef enumeration SYSTEMTYPEECONOMIZER;
	typedef enumeration SYSTEMPLANTMODE;
	typedef enumeration SYSTEMTYPEVENTILATION; 
	typedef set UNITAPPLIANCETYPE;
	typedef set UNITSYSTEMTYPE;

public:

	// global variables
	static char1024 load_property;
	static double maximum_temperature_update;
	static TIMESTAMP maximum_timestep;

public:

	// published variables
	GL_ATOMIC(int16,building_floors); // must be set by user
	GL_ATOMIC(double,building_floor_depth);
	GL_ATOMIC(double,building_floor_height);
	GL_ATOMIC(double,building_occupancy_factor);
	GL_ATOMIC(double,building_outdoor_temperature);
	GL_ATOMIC(int16,building_units); // must be set by user

	GL_ATOMIC(double,core_cooling_setpoint);
	GL_ATOMIC(CORECONFIGURATION,core_configuration);
	GL_ATOMIC(int16,core_elevators);
	GL_ATOMIC(double,core_heating_setpoint);
	GL_ATOMIC(SYSTEMPLANTMODE,core_mode);
	GL_ATOMIC(int16,core_laundry_units);
	GL_ATOMIC(double,core_width);

	GL_ATOMIC(double,parking_capacity_chargers);
	GL_ATOMIC(double,parking_capacity_elevators);
	GL_ATOMIC(double,parking_capacity_lights);
	GL_ATOMIC(double,parking_capacity_ventilation);
	GL_ATOMIC(int16,parking_chargers_active);
	GL_ATOMIC(int16,parking_chargers_installed);
	GL_ATOMIC(PARKINGCONFIGURATION,parking_configuration);
	GL_ATOMIC(double,parking_demand_chargers);
	GL_ATOMIC(double,parking_demand_elevators);
	GL_ATOMIC(double,parking_demand_lights);
	GL_ATOMIC(double,parking_demand_ventilation);
	GL_ATOMIC(int16,parking_size);

	GL_ATOMIC(double,power_core);
	GL_ATOMIC(double,power_parking);
	GL_ATOMIC(double,power_system);
	GL_ATOMIC(double,power_total);
	GL_ATOMIC(double,power_units);

	GL_ATOMIC(double,system_cooling_air_temperature);
	GL_ATOMIC(double,system_cooling_capacity);
	GL_ATOMIC(double,system_cooling_efficiency);
	GL_ATOMIC(double,system_heating_air_temperature);
	GL_ATOMIC(double,system_heating_capacity);
	GL_ATOMIC(double,system_heating_efficiency);
	GL_ATOMIC(SYSTEMPLANTMODE,system_mode);
	GL_ATOMIC(SYSTEMTYPECENTRAL,system_type_central);
	GL_ATOMIC(SYSTEMTYPEECONOMIZER,system_type_economizer);
	GL_ATOMIC(SYSTEMTYPEVENTILATION,system_type_ventilation);

	GL_ATOMIC(UNITAPPLIANCETYPE,unit_appliance_types);
	GL_ATOMIC(double,unit_capacity_cooking);
	GL_ATOMIC(double,unit_capacity_dishwasher);
	GL_ATOMIC(double,unit_capacity_dryer);
	GL_ATOMIC(double,unit_capacity_lights);
	GL_ATOMIC(double,unit_capacity_plugs);
	GL_ATOMIC(double,unit_capacity_refrigerator);
	GL_ATOMIC(double,unit_capacity_washer);
	GL_ATOMIC(double,unit_cooling_capacity);
	GL_ATOMIC(double,unit_cooling_efficiency);
	GL_ATOMIC(double,unit_cooling_setpoint);
	GL_ATOMIC(double,unit_demand_cooking);
	GL_ATOMIC(double,unit_demand_dishwasher);
	GL_ATOMIC(double,unit_demand_dryer);
	GL_ATOMIC(double,unit_demand_lights);
	GL_ATOMIC(double,unit_demand_plugs);
	GL_ATOMIC(double,unit_demand_refrigerator);
	GL_ATOMIC(double,unit_demand_washer);
	GL_ATOMIC(double,unit_depth);
	GL_ATOMIC(double,unit_heating_capacity);
	GL_ATOMIC(double,unit_heating_efficiency);
	GL_ATOMIC(double,unit_heating_setpoint);
	GL_ATOMIC(SYSTEMPLANTMODE,unit_mode);
	GL_ATOMIC(UNITSYSTEMTYPE,unit_system_type);
	GL_ATOMIC(double,unit_width);

	GL_ATOMIC(double,vacant_cooling_setpoint);
	GL_ATOMIC(double,vacant_heating_setpoint);
	GL_ATOMIC(SYSTEMPLANTMODE,vacant_mode);

private:

	// thermal properties
	double U_OA;
	double U_OU;
	double U_OC;
	double U_OM;
	double U_AU;
	double U_AC;
	double U_AM;
	double U_UC;
	double U_UM;
	double U_CM;
	
	// zone capacitance
	double C_A;
	double C_U;
	double C_C;
	double C_M;
	
	// zone heat gains
	double Q_AS;
	double Q_AV;
	double Q_AE;
	double Q_US;
	double Q_CS;
	double Q_CV;
	
	// modes
	int mode; // central system mode
	matrix m; // zone modes

	// temperature (w.r.t. apartment setpoint temperature
	double Tout;
	matrix Tbal;
	matrix Teq;

	// input constraints
	matrix u_min;
	matrix u_max;

	// internal model 
	matrix A, Ainv, Aeig;
	matrix B1, B1inv;
	matrix B2, B2inv;

	// model inputs
	matrix q;
	matrix u;

	// state variables
	matrix T;
	matrix dT;

	// helper methods
	matrix update_u(void);

public:

	// required methods
	apartment(MODULE *module);
	int create(void);
	int init(OBJECT *parent);
	TIMESTAMP precommit(TIMESTAMP t1);

public:

	// required support variables
	static CLASS *oclass;
	static apartment *defaults;
};

#endif // _G_APARTMENT_H