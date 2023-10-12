#include "Patch.h"
#include "Diffuser.h"
#include "Reverb.h"
#include "custom_dsp.h"

class ReverbPatch : public Patch
{
public:
	Diffuser* diffuser;
	Reverb* reverb;

	ReverbPatch() : Patch()
	{
		diffuser = Diffuser::create();
		reverb = Reverb::create(getSampleRate());
		registerParameter(PARAMETER_A, "Reverb");
		registerParameter(PARAMETER_B, "Feedback");
		setParameterValue(PARAMETER_A, 0);
	}

	~ReverbPatch()
	{
		Reverb::destroy(reverb);
	}

	void processAudio(AudioBuffer& audio) override
	{
		float reverbAmount = getParameterValue(PARAMETER_A);
		float feedback = getParameterValue(PARAMETER_B);
		float freeze = 1;

    float reverbLevel = reverbAmount * 0.95f;
    reverbLevel += feedback * (2.0f - feedback) * freeze;
    reverbLevel = daisysp::fclamp(reverbLevel, 0.0f, 1.0f);

		//diffuser->setAmount(reverbLevel);
		//diffuser->process(audio, audio);

    reverb->setAmount(reverbLevel * 0.54f);
    reverb->setDiffusion(0.7f);
    reverb->setReverbTime(0.35f + 0.63f * reverbLevel);
    reverb->setInputGain(0.2f);
    reverb->setLowPass(0.6f + 0.37f * feedback);
    reverb->process(audio, audio);
	}

};
 