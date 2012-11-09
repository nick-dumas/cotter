#ifndef AVERAGING_MS_WRITER_H
#define AVERAGING_MS_WRITER_H

#include "mswriter.h"

#include <iostream>

class UVWCalculater
{
	public:
		virtual void CalculateUVW(double date, size_t antenna1, size_t antenna2, double &u, double &v, double &w) = 0;
};

class AveragingMSWriter : public Writer
{
	public:
		AveragingMSWriter(const char *filename, size_t timeCount, size_t freqAvgFactor, UVWCalculater &uvwCalculater)
		: _writer(filename), _timeAvgFactor(timeCount), _freqAvgFactor(freqAvgFactor),
		_originalChannelCount(0), _avgChannelCount(0), _antennaCount(0), _uvwCalculater(uvwCalculater)
		{
		}
		
		~AveragingMSWriter()
		{
			destroyBuffers();
		}
		
		void WriteBandInfo(const std::string &name, const std::vector<MSWriter::ChannelInfo> &channels, double refFreq, double totalBandwidth, bool flagRow)
		{
			if(channels.size()%_freqAvgFactor != 0)
			{
				std::cout << " Warning: channels averaging factor is not a multiply of total number of channels. Last channel(s) will be left out.\n";
			}
			
			_avgChannelCount = channels.size() / _freqAvgFactor;
			_originalChannelCount = channels.size();
			
			std::vector<MSWriter::ChannelInfo> avgChannels(_avgChannelCount);
			for(size_t ch=0; ch!=_avgChannelCount; ++ch)
			{
				MSWriter::ChannelInfo channel;
				channel.chanFreq = 0.0;
				channel.chanWidth = 0.0;
				channel.effectiveBW = 0.0;
				channel.resolution = 0.0;
				for(size_t i=0; i!=_freqAvgFactor; ++i)
				{
					const MSWriter::ChannelInfo& curChannel = channels[ch*_freqAvgFactor + i];
					channel.chanFreq += curChannel.chanFreq;
					channel.chanWidth += curChannel.chanWidth;
					channel.effectiveBW += curChannel.effectiveBW;
					channel.resolution += curChannel.resolution;
				}
				
				channel.chanFreq /= (double) _freqAvgFactor;
				channel.chanWidth /= (double) _freqAvgFactor;
				channel.effectiveBW /= (double) _freqAvgFactor;
				channel.resolution /= (double) _freqAvgFactor;
				
				avgChannels[ch] = channel;
			}
			
			_writer.WriteBandInfo(name, avgChannels, refFreq, totalBandwidth, flagRow);
			
			if(_antennaCount != 0)
				initBuffers();
		}
		
		void WriteAntennae(const std::vector<MSWriter::AntennaInfo> &antennae)
		{
			_writer.WriteAntennae(antennae);
			
			_antennaCount = antennae.size();
			if(_originalChannelCount != 0)
				initBuffers();
		}
		
		void WritePolarizationForLinearPols(bool flagRow)
		{
			_writer.WritePolarizationForLinearPols(flagRow);
		}
		
		void WriteField(const MSWriter::FieldInfo& field)
		{
			_writer.WriteField(field);
		}
		
		void WriteRow(double time, size_t antenna1, size_t antenna2, double u, double v, double w, const std::complex<float>* data, const bool* flags)
		{
			Buffer &buffer = getBuffer(antenna1, antenna2);
			for(size_t ch=0; ch!=_avgChannelCount*_freqAvgFactor; ++ch)
			{
				for(size_t p=0; p!=4; ++p)
				{
					if(!flags[ch*4 + p])
					{
						size_t destIndex = (ch / _freqAvgFactor) * 4 + p;
						buffer._rowData[destIndex] += data[ch*4 + p];
						buffer._rowWeights[destIndex] += 1.0;
						buffer._rowCounts[destIndex]++;
					}
				}
			}
			buffer._rowTime += time;
			buffer._rowTimestepCount++;
			
			if(buffer._rowTimestepCount == _timeAvgFactor)
				writeCurrentTimestep(antenna1, antenna2);
		}
		
	private:
		struct Buffer
		{
			Buffer(size_t avgChannelCount)
			{
				_rowData = new std::complex<float>[avgChannelCount*4];
				_rowFlags = new bool[avgChannelCount*4];
				_rowWeights = new float[avgChannelCount*4];
				_rowCounts = new size_t[avgChannelCount*4];
				initZero(avgChannelCount);
			}
			
			~Buffer()
			{
				delete[] _rowData;
				delete[] _rowFlags;
				delete[] _rowWeights;
				delete[] _rowCounts;
			}
			
			void initZero(size_t avgChannelCount)
			{
				_rowTime = 0.0;
				_rowTimestepCount = 0;
				for(size_t ch=0; ch!=avgChannelCount*4; ++ch)
				{
					_rowData[ch] = 0.0;
					_rowFlags[ch] = false;
					_rowWeights[ch] = 0.0;
					_rowCounts[ch] = 0;
				}
			}
			
			double _rowTime;
			size_t _rowTimestepCount;
			std::complex<float> *_rowData;
			bool *_rowFlags;
			float *_rowWeights;
			size_t *_rowCounts;
		};
		
		void writeCurrentTimestep(size_t antenna1, size_t antenna2)
		{
			Buffer& buffer = getBuffer(antenna1, antenna2);
			double time = buffer._rowTime / buffer._rowTimestepCount;
			double u, v, w;
			_uvwCalculater.CalculateUVW(time, antenna1, antenna2, u, v, w);
			
			for(size_t ch=0;ch!=_avgChannelCount*4;++ch)
			{
				buffer._rowData[ch] /= buffer._rowCounts[ch];
				buffer._rowFlags[ch] = (buffer._rowCounts[ch]==0);
			}
			
			_writer.WriteRow(time, antenna1, antenna2, u, v, w, buffer._rowData, buffer._rowFlags);
			
			buffer.initZero(_avgChannelCount);
		}
		
		Buffer &getBuffer(size_t antenna1, size_t antenna2)
		{
			return *_buffers[antenna1*_antennaCount + antenna2];
		}
		
		void setBuffer(size_t antenna1, size_t antenna2, Buffer *buffer)
		{
			_buffers[antenna1*_antennaCount + antenna2] = buffer;
		}
		
		void initBuffers()
		{
			destroyBuffers();
			_buffers.resize(_antennaCount*_antennaCount);
			for(size_t antenna1=0; antenna1!=_antennaCount; ++antenna1)
			{
				for(size_t antenna2=0; antenna2!=antenna1; ++antenna2)
					setBuffer(antenna1, antenna2, 0);
				
				for(size_t antenna2=antenna1; antenna2!=_antennaCount; ++antenna2)
				{
					Buffer *buffer = new Buffer(_avgChannelCount*4);
					setBuffer(antenna1, antenna2, buffer);
				}
			}
		}
		
		void destroyBuffers()
		{
			if(!_buffers.empty())
			{
				for(size_t antenna1=0; antenna1!=_antennaCount; ++antenna1)
				{
					for(size_t antenna2=antenna1; antenna2!=_antennaCount; ++antenna2)
						delete &getBuffer(antenna1, antenna2);
				}
			}
			_buffers.clear();
		}
		
		MSWriter _writer;
		size_t _timeAvgFactor, _freqAvgFactor;
		size_t _originalChannelCount, _avgChannelCount, _antennaCount;
		UVWCalculater &_uvwCalculater;
		std::vector<Buffer*> _buffers;
};

#endif
