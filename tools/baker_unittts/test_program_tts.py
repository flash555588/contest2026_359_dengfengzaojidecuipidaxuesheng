"""Direct targets must be achieved beyond two semitones, never silently clipped."""
from types import SimpleNamespace
import unittest
import numpy as np

from .dsp_controls import render_controls
from .program_tts import ProgramCosts,inspect_output,phase_time
from .synthesize import SourceContext


class ProgramTests(unittest.TestCase):
    def source(self):
        t=np.arange(48000)/48000
        return np.rint(7000*(np.sin(2*np.pi*240*t)+.2*np.sin(2*np.pi*480*t))).astype(np.int16)

    def test_six_semitone_goal_is_measured_in_output(self):
        target=240*2**(.5)
        output,detail=render_controls(self.source(),48000,[0.,.5,1.],[target]*3,
                                      pitch_limit_semitones=12.,clip_pitch=False)
        plan=dict(tokens=['a1'],phone_durations=[[1.]],f0=[[target]*24],voiced=[[1.]*24])
        r=inspect_output(output,48000,[dict(target_index=0,unit_id=1,output_start=0,output_end=len(output))],plan)[0]
        self.assertLess(r['mean_f0_error_st'],.15)
        self.assertEqual(detail['pitch_points_limited'],0)
        self.assertFalse(detail['pitch_target_clipping'])

    def test_falling_contour_goal_is_not_only_a_constant_pitch_shift(self):
        times=np.linspace(0,1,101);pitch=320*2**(-times)
        output,_=render_controls(self.source(),48000,times,pitch,pitch_limit_semitones=12.,clip_pitch=False)
        query=(np.arange(24)+.5)/24
        plan=dict(tokens=['a4'],phone_durations=[[1.]],f0=[(320*2**(-query)).tolist()],voiced=[[1.]*24])
        r=inspect_output(output,48000,[dict(target_index=0,unit_id=1,output_start=0,output_end=len(output))],plan)[0]
        self.assertLess(r['mean_f0_error_st'],.35)

    def test_out_of_range_pitch_is_rejected_instead_of_clipped(self):
        with self.assertRaisesRegex(ValueError,'reselect'):
            render_controls(self.source(),48000,[0.,1.],[550.,550.],pitch_limit_semitones=12.,clip_pitch=False)

    def test_program_uses_full_contour_and_rejects_wrong_topology(self):
        units=[SimpleNamespace(uid=i,token='wu3',utterance=str(i)) for i in (1,2,3)]
        inv=SimpleNamespace(by_token={'wu3':units},sources={str(i):{} for i in (1,2,3)},
                            source_context=lambda u:SourceContext(0,1,None,None))
        goal=np.r_[np.linspace(230,180,12),np.linspace(180,190,12)]
        def acoustic(u):
            values=np.r_[np.linspace(250,330,12),np.linspace(330,200,12)] if u.uid==1 else goal
            return dict(phones=np.array(['u3' if u.uid!=3 else 'a3']),durations=np.array([.2]),
                        log_f0=np.log(values),voiced=np.ones(24))
        store=SimpleNamespace(unit=acoustic)
        plan=dict(phones=[['u3']],phone_durations=[[.2]],f0=[goal],voiced=[np.ones(24)])
        provider=ProgramCosts(inv,store,plan)
        values=provider.costs(inv,['wu3'],0,None)
        self.assertLess(values[2],values[1]);self.assertFalse(np.isfinite(values[3]))
        self.assertGreater(provider.details[0][1]['max_pitch_correction_st'],2)

    def test_phone_time_map_is_monotonic_and_hits_boundaries(self):
        mapped=phase_time(np.array([0.,.1,.4]),np.array([.1,.3]),np.array([.2,.2]))
        np.testing.assert_allclose(mapped,[0.,.2,.4])


if __name__=='__main__':unittest.main()
