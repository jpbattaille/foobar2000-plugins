#define _WIN32_WINNT 0x0501
#include "../SDK/foobar2000.h"
#include "../ATLHelpers/ATLHelpers.h"
#include "resource.h"
#include "rubberband-1.8.1/rubberband/RubberBandStretcher.h"
#include "circular_buffer.h"
using namespace RubberBand;

static void RunDSPConfigPopup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );

#define BUFFER_SIZE 2048

class dsp_pitch : public dsp_impl_base
{
	RubberBandStretcher * rubber;
	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	circular_buffer<float>sample_buffer;
	pfc::array_t<float>samplebuf;
	float **plugbuf;
	float **m_scratch;

	unsigned buffered;
	bool st_enabled;
private:
	void insert_chunks()
	{
		
		t_size samples = rubber->available();
		if (!samples) return;
		samplebuf.grow_size(BUFFER_SIZE * m_ch);
		do
		{

			samples = rubber->retrieve(m_scratch,BUFFER_SIZE);
			if (samples > 0)
			{
				samplebuf.grow_size(samples*m_ch);
				float *data = samplebuf.get_ptr();
				for (int c = 0; c < m_ch; ++c) {
					int j = 0;
					while (j < buffered) {
						data[j * m_ch + c] = m_scratch[c][j];
						++j;
					}
				}
				audio_chunk * chunk = insert_chunk(samples);
				chunk->set_data(data,samples,m_ch,m_rate);
			}
		}
		while (samples != 0);
	}


public:
	dsp_pitch( dsp_preset const & in ) : pitch_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		buffered=0;
		rubber = 0;
		plugbuf = NULL;
		parse_preset( pitch_amount, in );
		st_enabled = true;
	}
	~dsp_pitch(){
		if (rubber) 
			delete rubber;
	    rubber = 0;
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// Create these with guidgen.exe.
		// {A7FBA855-56D4-46AC-8116-8B2A8DF2FB34}
		static const GUID guid = 
		{ 0xabc792be, 0x276, 0x47bf, { 0xb2, 0x41, 0x7a, 0xcf, 0xc5, 0x21, 0xcb, 0x50 } };


		return guid;
	}

	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Pitch Shift (Rubber band)";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
	}

	virtual void on_endofplayback(abort_callback & p_abort) {
		//same as flush, only at end of playback
	/*	if (rubber && st_enabled)
		{
			insert_chunks();
			if (buffered)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				rubber->process((float *const *)samplebuf.get_ptr(),buffered,false);
				buffered = 0;
			}
			insert_chunks();	
		}*/
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

		if ( chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask )
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();

			RubberBandStretcher::Options options = RubberBandStretcher::DefaultOptions;
			options |= RubberBandStretcher::OptionProcessRealTime;
		//	double pitch_amount = pow(2.0,pitch_amount/12.0);
			rubber = new RubberBandStretcher(m_rate,m_ch,options,1.0, 1.0);
			if (!rubber) return 0;
			sample_buffer.set_size(BUFFER_SIZE*m_ch);
			if(plugbuf)delete plugbuf;
			plugbuf = new float*[m_ch];
			m_scratch = new float*[m_ch];

			for (int c = 0; c < m_ch; ++c) plugbuf[c] = new float[BUFFER_SIZE];
			for (int c = 0; c < m_ch; ++c) m_scratch[c] = new float[BUFFER_SIZE];
			st_enabled = true;
		//	if (pitch_amount== 0)st_enabled = false;
			bool usequickseek = false;
			bool useaafilter = false; //seems clearer without it
		}
		samplebuf.grow_size(BUFFER_SIZE*m_ch);

		if (!st_enabled) return true;

		while (sample_count > 0)
		{    
			int todo = min(BUFFER_SIZE - buffered, sample_count);
			sample_buffer.write(src,todo*m_ch);
			src += todo * m_ch;
			buffered += todo;
			sample_count -= todo;

			
			

			if (buffered == BUFFER_SIZE)
			{

				float*data = samplebuf.get_ptr();
				sample_buffer.read((float*)data,buffered*m_ch);

				for (int c = 0; c < m_ch; ++c) {
					 int j = 0;
					 while (j < buffered) {
				     plugbuf[c][j] = data[j * m_ch + c];
						   ++j;
					 }
			    }

				rubber->setMaxProcessSize(buffered);
				rubber->process(plugbuf,buffered,true);
				insert_chunks();
				buffered = 0;
			}
		}
		return false;
	}

	virtual void flush() {
		if (rubber){
			rubber->reset();
		}
		m_rate = 0;
		m_ch = 0;
		buffered = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		return (rubber && m_rate && st_enabled) ? ((double)(rubber->available() + buffered) / (double)m_rate) : 0;
	}


	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset( dsp_preset & p_out )
	{
		make_preset( 0.0, p_out );
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopup( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset( float pitch, dsp_preset & out )
	{
		dsp_preset_builder builder; 
		builder << pitch; 
		builder.finish( g_get_guid(), out );
	}                        
	static void parse_preset(float & pitch, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch; 
		}
		catch(exception_io_data) {pitch = 0.0;}
	}
};

class CMyDSPPopupPitch : public CDialogImpl<CMyDSPPopupPitch>
{
public:
	CMyDSPPopupPitch( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_PITCH };
	enum
	{
		pitchmin = 0,
		pitchmax = 4800

	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		MSG_WM_HSCROLL( OnHScroll )
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_PITCH);
		slider_drytime.SetRange(0, pitchmax);

		{
			float  pitch;
			dsp_pitch::parse_preset(pitch, m_initData);
			pitch *= 100.00;
			slider_drytime.SetPos( (double)(pitch+2400));
			RefreshLabel( pitch/100.00);
		}
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnHScroll( UINT nSBCode, UINT nPos, CScrollBar pScrollBar )
	{
		float pitch;
		pitch = slider_drytime.GetPos()-2400;
		pitch /= 100.00;
		{
			dsp_preset_impl preset;
			dsp_pitch::make_preset(pitch, preset );
			m_callback.on_preset_changed( preset );
		}
		RefreshLabel( pitch);
	}

	void RefreshLabel(float  pitch )
	{
		pfc::string_formatter msg; 
		msg << "Pitch: ";
		msg << (pitch < 0 ? "" : "+");
		msg << pfc::format_float(pitch,0,2) << " semitones";
		::uSetDlgItemText( *this, IDC_PITCHINFO, msg );
		msg.reset();
	}
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime,slider_wettime,slider_dampness,slider_roomwidth,slider_roomsize;
};
static void RunDSPConfigPopup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
{
	CMyDSPPopupPitch popup( p_data, p_callback );
	if ( popup.DoModal(p_parent) != IDOK ) p_callback.on_preset_changed( p_data );
}

static dsp_factory_t<dsp_pitch> g_dsp_pitch_factory;