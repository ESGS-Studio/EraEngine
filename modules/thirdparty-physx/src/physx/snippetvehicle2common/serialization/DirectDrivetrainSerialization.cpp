#include <pch.h>
#ifdef setBit
#undef setBit
#endif // setBit
#include "DirectDrivetrainSerialization.h"
#include "SerializationCommon.h"

#include <fstream>
#include <sstream>

namespace snippetvehicle2
{

bool readThrottleResponseParams
(const rapidjson::Document& config, const PxVehicleAxleDescription& axleDesc,
	PxVehicleDirectDriveThrottleCommandResponseParams& throttleResponseParams)
{
	if (!config.HasMember("ThrottleCommandResponseParams"))
		return false;

	if (!readCommandResponseParams(config["ThrottleCommandResponseParams"], axleDesc, throttleResponseParams))
		return false;

	return true;
}

bool writeThrottleResponseParams
(const PxVehicleDirectDriveThrottleCommandResponseParams& throttleResponseParams, const PxVehicleAxleDescription& axleDesc,
 rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer)
{
	writer.Key("ThrottleCommandResponseParams");
	writer.StartObject();
	writeCommandResponseParams(throttleResponseParams, axleDesc, writer);
	writer.EndObject();
	return true;
}

bool readDirectDrivetrainParamsFromJsonFile(const char* directory, const char* filename,
	const PxVehicleAxleDescription& axleDescription, DirectDrivetrainParams& directDrivetrainParams)
{
	rapidjson::Document config;
	if (!openDocument(directory, filename, config))
		return false;

	if(!readThrottleResponseParams(config, axleDescription, directDrivetrainParams.directDriveThrottleResponseParams))
		return false;

	return true;
}

bool writeDirectDrivetrainParamsToJsonFile(const char* directory, const char* filename,
	const PxVehicleAxleDescription& axleDescription, const DirectDrivetrainParams& directDrivetrainParams)
{
	rapidjson::StringBuffer strbuf;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(strbuf);

	writer.StartObject();
	writeThrottleResponseParams(directDrivetrainParams.directDriveThrottleResponseParams, axleDescription, writer);
	writer.EndObject();

	std::ofstream myfile;
	myfile.open(std::string(directory) + "/" + filename);
	myfile << strbuf.GetString() << std::endl;
	myfile.close();

	return true;
}

}//namespace snippetvehicle2
