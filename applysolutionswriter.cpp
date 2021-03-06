
#include "applysolutionswriter.h"
#include "solutionfile.h"
#include "matrix2x2.h"

#include <complex>

ApplySolutionsWriter::ApplySolutionsWriter(std::unique_ptr<Writer> parentWriter, const std::string& filename) :
	ForwardingWriter(std::move(parentWriter)),
	_nChannels(0)
{
	SolutionFile solutionFile;
	solutionFile.OpenForReading(filename.c_str());
	
	if(solutionFile.IntervalCount() != 1)
		throw std::runtime_error("The provided solution file has multiple intervals, which is not supported. ");
	if(solutionFile.PolarizationCount() != 4)
		throw std::runtime_error("The provided solution file does not have 4 polarizations, which is not supported. ");
	
	_nSolutionChannels = solutionFile.ChannelCount();
	_nSolutionAntennas = solutionFile.AntennaCount();
	_solutions.resize(_nSolutionAntennas * _nSolutionChannels);
	size_t index = 0;
	for(size_t a = 0; a!=_nSolutionAntennas; ++a) {
		for(size_t ch = 0; ch!=_nSolutionChannels; ++ch) {
			for(size_t p = 0; p!=4; ++p) {
				_solutions[index][p] = solutionFile.ReadNextSolution();
			}
			++index;
		}
	}
}

ApplySolutionsWriter::~ApplySolutionsWriter()
{ }

void ApplySolutionsWriter::WriteBandInfo(const std::string &name, const std::vector<Writer::ChannelInfo> &channels, double refFreq, double totalBandwidth, bool flagRow)
{
	_nChannels = channels.size();
	_correctedData.resize(_nChannels*4);
	
	ForwardingWriter::WriteBandInfo(name, channels, refFreq, totalBandwidth, flagRow);

	if(_nChannels < _nSolutionChannels || (_nChannels % _nSolutionChannels) != 0) {
		std::ostringstream s;
		s << "The provided solution file has an incorrect number of channels. Oobservation has " << _nChannels << " channels, and solution file has " << _nSolutionChannels << " channels.";
		throw std::runtime_error(s.str());
	}
}

void ApplySolutionsWriter::WriteRow(double time, double timeCentroid, size_t antenna1, size_t antenna2, double u, double v, double w, double interval, const std::complex<float>* data, const bool* flags, const float* weights)
{
	// Apply solution to averaged data
	int channelRatio = _nChannels / _nSolutionChannels;
	
	const MC2x2* solA = &_solutions[antenna1 * _nSolutionChannels];
	const MC2x2* solB = &_solutions[antenna2 * _nSolutionChannels];
	MC2x2 scratch;
	
	for(size_t ch=0; ch!=_nChannels; ch++)
	{
		size_t solChannel = ch / channelRatio;
	
		MC2x2 dataAsDouble(data[ch * 4], data[ch * 4 +1], data[ch * 4 +2], data[ch * 4 +3]);
		MC2x2::ATimesB(scratch, solA[solChannel], dataAsDouble);
		MC2x2::ATimesHermB(dataAsDouble, scratch, solB[solChannel]);
		for(size_t p=0; p!=4; ++p)
			_correctedData[ch * 4 + p] = dataAsDouble[p];
	}
	
	ForwardingWriter::WriteRow(time, timeCentroid, antenna1, antenna2, u, v, w, interval, _correctedData.data(), flags, weights);
}
