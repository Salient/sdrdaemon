///////////////////////////////////////////////////////////////////////////////////
// SDRdaemon - send I/Q samples read from a SDR device over the network via UDP. //
//                                                                               //
// Copyright (C) 2015 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <climits>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <rtl-sdr.h>

#include "util.h"
#include "parsekv.h"
#include "RtlSdrSource.h"

RtlSdrSource *RtlSdrSource::m_this = 0;

// Open RTL-SDR device.
RtlSdrSource::RtlSdrSource(int dev_index) :
    m_dev(0),
    m_block_length(default_block_length),
    m_thread(0)
{
    int r;

    const char *devname = rtlsdr_get_device_name(dev_index);
    if (devname != NULL)
        m_devname = devname;

    r = rtlsdr_open(&m_dev, dev_index);

    if (r < 0)
    {
        m_error =  "Failed to open RTL-SDR device (";
        m_error += strerror(-r);
        m_error += ")";
    }
    else
    {
        m_gains = get_tuner_gains();
        std::ostringstream gains_ostr;

        gains_ostr << std::fixed << std::setprecision(1);

        for (int g: m_gains)
        {
            gains_ostr << 0.1 * g << " ";
        }

        m_gainsStr = gains_ostr.str();
    }

    m_this = this;
}


// Close RTL-SDR device.
RtlSdrSource::~RtlSdrSource()
{
    if (m_dev)
        rtlsdr_close(m_dev);

    m_this = 0;
}

bool RtlSdrSource::configure(parsekv::pairs_type& m)
{
    uint32_t sample_rate = 1000000;
    uint32_t frequency = m_confFreq;
    int32_t  ppm = 0;
    bool ppmp = false;
    int tuner_gain = INT_MIN;
    int block_length =  default_block_length;
    bool agcmode = false;
    int fcpos = 2; // default is center
    std::uint32_t changeFlags = 0;

	if (m.find("srate") != m.end())
	{
		std::cerr << "RtlSdrSource::configure(m): srate: " << m["srate"] << std::endl;
		sample_rate = atoi(m["srate"].c_str());

		if ((sample_rate < 225001)
				|| ((sample_rate > 300000) && (sample_rate < 900001))
				|| (sample_rate > 3200000))
		{
			m_error = "Invalid sample rate";
			return false;
		}

		changeFlags |= 0x1;

        if (m_fcPos != 2)
        {
            changeFlags |= 0x2; // need to adjust actual center frequency if not centered
        }        
	}

	if (m.find("freq") != m.end())
	{
		std::cerr << "RtlSdrSource::configure(m): freq: " << m["freq"] << std::endl;
		frequency = atoi(m["freq"].c_str());

		if ((frequency < 10000000) || (frequency > 2200000000))
		{
			m_error = "Invalid frequency";
			return false;
		}

		changeFlags |= 0x2;
	}

	if (m.find("ppmp") != m.end())
	{
		std::cerr << "RtlSdrSource::configure(m): ppmp: " << m["ppmp"] << std::endl;
		ppm = atoi(m["ppmp"].c_str());

		changeFlags |= 0x4;
		ppmp = true;
	}

	if ((m.find("ppmn") != m.end()) && !ppmp)
	{
		std::cerr << "RtlSdrSource::configure(m): ppmn: " << m["ppmn"] << std::endl;
		ppm = -atoi(m["ppmn"].c_str());

		changeFlags |= 0x4;
	}

	if (m.find("gain") != m.end())
	{
		std::string gain_str = m["gain"];
		std::cerr << "RtlSdrSource::configure(m): gain: " << gain_str << std::endl;

		if (strcasecmp(gain_str.c_str(), "auto") == 0)
		{
			tuner_gain = INT_MIN;
		}
		else if (strcasecmp(gain_str.c_str(), "list") == 0)
		{
			m_error = "Available gains (dB): " + m_gainsStr;
			return false;
		}
		else
		{
			double tmpgain;

			if (!parse_dbl(gain_str.c_str(), tmpgain))
			{
				m_error = "Invalid gain";
				return false;
			}
			else
			{
				long int tmpgain2 = lrint(tmpgain * 10);

				if (tmpgain2 <= INT_MIN || tmpgain2 >= INT_MAX) {
					m_error = "Invalid gain";
					return false;
				}
				else
				{
					tuner_gain = tmpgain2;

					if (find(m_gains.begin(), m_gains.end(), tuner_gain) == m_gains.end())
					{
						m_error = "Gain not supported. Available gains (dB): " + m_gainsStr;
						return false;
					}
				}
			}
		}

		changeFlags |= 0x8;
	} // gain

	if (m.find("agc") != m.end())
	{
		std::cerr << "RtlSdrSource::configure(m): agc" << std::endl;
		agcmode = true;

		changeFlags |= 0x10;
	}

	if (m.find("blklen") != m.end())
	{
		std::cerr << "RtlSdrSource::configure(m): blklen: " << m["blklen"] << std::endl;
		block_length = atoi(m["blklen"].c_str());

		changeFlags |= 0x20;
	}

	if (m.find("fcpos") != m.end())
	{
		std::cerr << "RtlSdrSource::configure(m): fcpos: " << m["fcpos"] << std::endl;
		fcpos = atoi(m["fcpos"].c_str());

		if ((fcpos < 0) || (fcpos > 2))
		{
			m_error = "Invalid center frequency position";
			return false;
		}
		else
		{
			m_fcPos = fcpos;
		}

        changeFlags |= 0x2; // need to adjust actual center frequency if not centered
	}

	if (m.find("decim") != m.end())
	{
		std::cerr << "RtlSdrSource::configure(m): decim: " << m["decim"] << std::endl;
		int log2Decim = atoi(m["decim"].c_str());

		if ((log2Decim < 0) || (log2Decim > 6))
		{
			m_error = "Invalid log2 decimation factor";
			return false;
		}
		else
		{
			m_decim = log2Decim;
		}
	}

	// Intentionally tune at a higher frequency to avoid DC offset.
	m_confFreq = frequency;
	m_confAgc = agcmode;
	double tuner_freq;

	if (m_fcPos == 0) { // Infradyne
		tuner_freq = frequency + 0.25 * sample_rate;
	} else if (m_fcPos == 1) { // Supradyne
		tuner_freq = frequency - 0.25 * sample_rate;
	} else { // Centered
		tuner_freq = frequency;
	}

	return configure(changeFlags, sample_rate, tuner_freq, ppm, tuner_gain, block_length, agcmode);
}

// Configure RTL-SDR tuner and prepare for streaming.
bool RtlSdrSource::configure(std::uint32_t changeFlags,
		std::uint32_t sample_rate,
		std::uint32_t frequency,
		std::int32_t  ppm,
        int tuner_gain,
        int block_length,
        bool agcmode)
{
    int r;

    if (!m_dev)
        return false;

    if (changeFlags & 0x1)
    {
		r = rtlsdr_set_sample_rate(m_dev, sample_rate);
		if (r < 0) {
			m_error = "rtlsdr_set_sample_rate failed";
			return false;
		}
    }

    if (changeFlags & 0x2)
    {
    	std::cerr << "RtlSdrSource::configure: freq: " << frequency << std::endl;
		r = rtlsdr_set_center_freq(m_dev, frequency);
		if (r < 0) {
			m_error = "rtlsdr_set_center_freq failed";
			return false;
		}
    }

    if (changeFlags & 0x4)
    {
		r = rtlsdr_set_freq_correction(m_dev, ppm);
		if (r < 0) {
			m_error = "rtlsdr_set_freq_correction failed";
			return false;
		}
    }

    if (changeFlags & 0x8)
    {
		if (tuner_gain == INT_MIN) {
			r = rtlsdr_set_tuner_gain_mode(m_dev, 0);
			if (r < 0) {
				m_error = "rtlsdr_set_tuner_gain_mode could not set automatic gain";
				return false;
			}
		} else {
			r = rtlsdr_set_tuner_gain_mode(m_dev, 1);
			if (r < 0) {
				m_error = "rtlsdr_set_tuner_gain_mode could not set manual gain";
				return false;
			}

			r = rtlsdr_set_tuner_gain(m_dev, tuner_gain);
			if (r < 0) {
				m_error = "rtlsdr_set_tuner_gain failed";
				return false;
			}
		}
    }

    if (changeFlags & 0x10)
    {
		// set RTL AGC mode
		r = rtlsdr_set_agc_mode(m_dev, int(agcmode));
		if (r < 0) {
			m_error = "rtlsdr_set_agc_mode failed";
			return false;
		}
    }

    if (changeFlags & 0x20)
    {
	   // set block length
		m_block_length = (block_length < 4096) ? 4096 :
						 (block_length > 1024 * 1024) ? 1024 * 1024 :
						 block_length;
		m_block_length -= m_block_length % 4096;
    }

    // reset buffer to start streaming
    if (rtlsdr_reset_buffer(m_dev) < 0) {
        m_error = "rtlsdr_reset_buffer failed";
        return false;
    }

    return true;
}


// Return current sample frequency in Hz.
uint32_t RtlSdrSource::get_sample_rate()
{
    return rtlsdr_get_sample_rate(m_dev);
}

// Return device current center frequency in Hz.
uint32_t RtlSdrSource::get_frequency()
{
    return rtlsdr_get_center_freq(m_dev);
}

void RtlSdrSource::print_specific_parms()
{
    int lnagain = get_tuner_gain();

    if (lnagain == INT_MIN)
        fprintf(stderr, "LNA gain:          auto\n");
    else
        fprintf(stderr, "LNA gain:          %.1f dB\n",
                0.1 * lnagain);

    fprintf(stderr, "RTL AGC mode:      %s\n",
            m_confAgc ? "enabled" : "disabled");
}

// Return current tuner gain in units of 0.1 dB.
int RtlSdrSource::get_tuner_gain()
{
    return rtlsdr_get_tuner_gain(m_dev);
}


// Return a list of supported tuner gain settings in units of 0.1 dB.
std::vector<int> RtlSdrSource::get_tuner_gains()
{
    int num_gains = rtlsdr_get_tuner_gains(m_dev, NULL);
    if (num_gains <= 0)
        return std::vector<int>();

    std::vector<int> gains(num_gains);
    if (rtlsdr_get_tuner_gains(m_dev, gains.data()) != num_gains)
        return std::vector<int>();

    return gains;
}

bool RtlSdrSource::start(DataBuffer<IQSample>* buf, std::atomic_bool *stop_flag)
{
    m_buf = buf;
    m_stop_flag = stop_flag;

    if (m_thread == 0)
    {
        m_thread = new std::thread(run);
        return true;
    }
    else
    {
        m_error = "Source thread already started";
        return false;
    }
}

bool RtlSdrSource::stop()
{
    if (m_thread)
    {
        m_thread->join();
        delete m_thread;
        m_thread = 0;
    }

    return true;
}

void RtlSdrSource::run()
{
    IQSampleVector iqsamples;
    void *msgBuf = 0;

    while (!m_this->m_stop_flag->load() && get_samples(&iqsamples))
    {
        m_this->m_buf->push(move(iqsamples));

        int len = nn_recv(m_this->m_nnReceiver, &msgBuf, NN_MSG, NN_DONTWAIT);

        if ((len > 0) && msgBuf)
        {
            std::string msg((char *) msgBuf, len);
            std::cerr << "RtlSdrSource::run: received: " << msg << std::endl;
            m_this->Source::configure(msg);
            nn_freemsg(msgBuf);
            msgBuf = 0;
        }
    }
}

// Fetch a bunch of samples from the device.
bool RtlSdrSource::get_samples(IQSampleVector *samples)
{
    int r, n_read;

    if (!m_this->m_dev) {
        return false;
    }

    if (!samples) {
        return false;
    }

    std::vector<uint8_t> buf(4 * m_this->m_block_length);

    r = rtlsdr_read_sync(m_this->m_dev, buf.data(), 4 * m_this->m_block_length, &n_read);

    if (r < 0)
    {
        m_this->m_error = "rtlsdr_read_sync failed";
        return false;
    }

    if (n_read != 4 * m_this-> m_block_length)
    {
        m_this->m_error = "short read, samples lost";
        return false;
    }

	if (m_this->m_decim == 0) // no decimation will take place
	{
	    samples->resize(m_this->m_block_length);

		for (int i = 0; i < m_this->m_block_length; i++)
		{
			// pack 8 bit samples onto 16 bit samples vector
			// invert I and Q because of the little Indians
			int16_t re_0 = buf[4*i] - 128;
			int16_t im_0 = buf[4*i+1] - 128;
			int16_t re_1 = buf[4*i+2] - 128;
			int16_t im_1 = buf[4*i+3] - 128;
			(*samples)[i] = IQSample((im_0<<8) | (re_0 & 0xFF), (im_1<<8) | (re_1 & 0xFF));
    	}
	}
   	else // as decimation will take place store samples in 16 bit slots
	{
	    samples->resize(2 * m_this->m_block_length);

		for (int i = 0; i < m_this->m_block_length; i++)
		{
			(*samples)[2*i]   = IQSample(buf[4*i]   - 128, buf[4*i+1] - 128);
			(*samples)[2*i+1] = IQSample(buf[4*i+2] - 128, buf[4*i+3] - 128);
		}
	}

    return true;
}


// Return a list of supported devices.
void RtlSdrSource::get_device_names(std::vector<std::string>& devices)
{
    char manufacturer[256], product[256], serial[256];
    int device_count = rtlsdr_get_device_count();

    if (device_count > 0)
    {
        devices.resize(device_count);
    }

    devices.clear();

    for (int i = 0; i < device_count; i++)
    {
        if (!rtlsdr_get_device_usb_strings(i, manufacturer, product, serial))
        {
            std::ostringstream name_ostr;
            name_ostr << manufacturer << " " << product << " " << serial;
            devices.push_back(name_ostr.str());
        }
    }
}

/* end */
