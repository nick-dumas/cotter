#include "flagwriter.h"

#include <stdexcept>
#include <cstdio>

#include "version.h"

const uint16_t
	FlagWriter::VERSION_MINOR = 0,
	FlagWriter::VERSION_MAJOR = 1;

FlagWriter::FlagWriter(const std::string &filename, int gpsTime, size_t timestepCount, size_t nSb, size_t nodeSbStart, size_t nodeSbEnd, const std::vector<size_t>& subbandToGPUBoxFileIndex) :
	_timestepCount(timestepCount),
	_antennaCount(0),
	_channelCount(0),
	_channelsPerGPUBox(0),
	_polarizationCount(0),
//	_rowStride(0),
	_rowsAdded(0),
	_rowsWritten(0),
	_nodeSbStart(nodeSbStart),
	_nodeSbEnd(nodeSbEnd),
	_nSb(nSb),
	_gpsTime(gpsTime),
	_files(nodeSbEnd - nodeSbStart),
	_subbandToGPUBoxFileIndex(subbandToGPUBoxFileIndex)
{
	if(_nodeSbEnd - _nodeSbStart == 0)
		throw std::runtime_error("Flagwriter was initialized with zero gpuboxes");
		
	size_t numberPos = filename.find("%%");
	if(numberPos == std::string::npos)
		throw std::runtime_error("When writing flag files, multiple files will be written. Therefore, the name of the flagfile should contain two percent symbols (\"%%\"), e.g. \"Flagfile%%.mwaf\". These will be replaced by the gpubox number.");
	std::string name(filename);
	for(size_t i=_nodeSbStart; i!=_nodeSbEnd; ++i)
	{
		size_t gpuBoxIndex = _subbandToGPUBoxFileIndex[i] + 1;
		name[numberPos] = (char) ('0' + (gpuBoxIndex/10));
		name[numberPos+1] = (char) ('0' + (gpuBoxIndex%10));
		
		// If the file already exists, remove it
		FILE *fp = std::fopen(name.c_str(), "r");
		if (fp != NULL) {
			std::fclose(fp);
			std::remove(name.c_str());
		}
  
		int status = 0;
		if(fits_create_file(&_files[i-_nodeSbStart], name.c_str(), &status))
			throwError(status, "Cannot open flag file " + name + " for writing.");
	}
}

FlagWriter::~FlagWriter()
{
	for(std::vector<fitsfile*>::iterator i=_files.begin(); i!=_files.end(); ++i)
	{
		int status = 0;
		fits_close_file(*i, &status);
	}
}

void FlagWriter::writeHeader()
{
	std::ostringstream formatOStr;
	formatOStr << _channelsPerGPUBox << 'X';
	std::string formatStr = formatOStr.str();
  const char *columnNames[] = {"FLAGS"};
  const char *columnFormats[] = {formatStr.c_str()};
  const char *columnUnits[] = {""};
	
	for(size_t i=0; i!=_nodeSbEnd-_nodeSbStart; ++i)
	{
		int status = 0;
		long dimensionZero = 0;
		
		fits_create_img(_files[i], BYTE_IMG, 0, &dimensionZero, &status);
		checkStatus(status);
		
		std::ostringstream versionStr;
		versionStr << VERSION_MAJOR << '.' << VERSION_MINOR;
		fits_update_key(_files[i], TSTRING, "VERSION", const_cast<char*>(versionStr.str().c_str()), NULL, &status);
		checkStatus(status);
		
		updateIntKey(i, "GPSTIME", _gpsTime);
		updateIntKey(i, "NCHANS", _channelsPerGPUBox);
		updateIntKey(i, "NANTENNA", _antennaCount);
		updateIntKey(i, "NSCANS", _timestepCount);
		updateIntKey(i, "NPOLS", 1);
		updateIntKey(i, "GPUBOXNO", _subbandToGPUBoxFileIndex[i+_nodeSbStart] + 1);
		
		fits_update_key(_files[i], TSTRING, "COTVER", const_cast<char*>(COTTER_VERSION_STR), NULL, &status);
		fits_update_key(_files[i], TSTRING, "COTVDATE", const_cast<char*>(COTTER_VERSION_DATE), NULL, &status);
		checkStatus(status);
		
		fits_create_tbl(_files[i], BINARY_TBL, 0 /*nrows*/, 1 /*tfields*/,
			const_cast<char**>(columnNames), const_cast<char**>(columnFormats),
			const_cast<char**>(columnUnits), 0, &status);
		checkStatus(status);
	}
}

void FlagWriter::setStride()
{
	if(_channelCount % (_nodeSbEnd-_nodeSbStart) != 0)
		throw std::runtime_error("Something is wrong: number of channels requested to be written by the flagwriter is not divisable by the gpubox count");
	
	_channelsPerGPUBox = _channelCount / _nSb;
	
	// we assume we write only one polarization here
//	_rowStride = (_channelsPerGPUBox + 7) / 8;
	
	_singlePolBuffer.resize(_channelsPerGPUBox);
//	_packBuffer.resize(_rowStride);
}

void FlagWriter::SetOffsetsPerGPUBox(const std::vector<int>& offsets)
{
	if(_rowsAdded != 0)
		throw std::runtime_error("SetOffsetsPerGPUBox() called after rows were added to flagwriter");
	_hduOffsets = offsets;
}

void FlagWriter::writeRow(size_t antenna1, size_t antenna2, const bool* flags)
{
	++_rowsWritten;
	const size_t baselineCount = _antennaCount * (_antennaCount+1) / 2;
	for(size_t subband=_nodeSbStart; subband != _nodeSbEnd; ++subband)
	{
		std::vector<unsigned char>::iterator singlePolIter = _singlePolBuffer.begin();
		for(size_t i=0; i!=_channelsPerGPUBox; ++i)
		{
			*singlePolIter = *flags ? 1 : 0;
			++flags;
			
			for(size_t p=1; p!=_polarizationCount; ++p)
			{
				*singlePolIter |= *flags ? 1 : 0;
				++flags;
			}
			++singlePolIter;
		}
		
		int status = 0;
		int offset = _hduOffsets[_subbandToGPUBoxFileIndex[subband]];
		if(_rowsWritten > offset * baselineCount + 1)
		{
			size_t unalignedRow = _rowsWritten - offset * baselineCount;
			ffpcl(_files[subband-_nodeSbStart], TBIT, 1 /*colnum*/, unalignedRow /*firstrow*/,
				1 /*firstelem*/, _channelsPerGPUBox /*nelements*/, &_singlePolBuffer[0], &status);
			checkStatus(status);
		}
	}
}
