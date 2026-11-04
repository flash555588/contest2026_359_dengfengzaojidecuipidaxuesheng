"""Global planner -> deterministic source search -> direct target PSOLA.

No learned candidate ranker or material-conditioned adjuster is loaded. True
target curves are never clipped to a training correction range. Feasibility,
actual output F0 and output durations are recorded separately from tone quality.
"""
from __future__ import annotations

from collections import OrderedDict
import json
from pathlib import Path

import numpy as np
import parselmouth

from .bank import digest_file
from .bank_review import verify_files
from .controls_audition import duration_tier,map_time
from .dsp_controls import render_controls
from .synthesize import bounded_join

KNOTS=(np.arange(24)+.5)/24
ST=12/np.log(2)


def phase_time(time,source_durations,target_durations):
    return np.interp(time,np.r_[0.,np.cumsum(source_durations)],np.r_[0.,np.cumsum(target_durations)])


def desired_for(acoustic,plan,index):
    labels=[str(p) for p in acoustic['phones'] if p]
    if labels!=plan['phones'][index]:
        raise ValueError('Source phone topology differs from global planner')
    source=np.asarray(acoustic['durations'][:len(labels)],float)
    target=np.asarray(plan['phone_durations'][index][:len(labels)],float)
    if np.any(target<=0) or not np.isfinite(target).all():raise ValueError('Invalid global duration target')
    mapped=phase_time(KNOTS*source.sum(),source,target)/target.sum()
    goal=np.interp(mapped,KNOTS,np.log(plan['f0'][index]))
    voiced=np.interp(mapped,KNOTS,plan['voiced'][index])
    return source,target,goal,voiced


class AcousticStore:
    def __init__(self,inventory,full_data=None):
        self.inventory=inventory;self.full_data=full_data;self.cache=OrderedDict()
        if inventory.bank_id:
            if not inventory.context_complete:raise ValueError('Program selection requires original source context')
            self.manifest=verify_files(inventory.path.parent)
        else:
            manifest=json.loads((full_data/'dataset.json').read_text())
            self.records={r['id']:r for r in manifest['records'] if r.get('split')=='train' and 'skipped' not in r}

    def unit(self,u):
        if self.inventory.bank_id:return self.inventory.acoustic_unit(u)
        uid=u.utterance
        if uid not in self.cache:
            record=self.records[uid];path=self.full_data/record['path']
            if digest_file(path)!=record['sha256']:raise ValueError('Original source features changed')
            with np.load(path,allow_pickle=False) as f:self.cache[uid]=dict(f)
            if len(self.cache)>96:self.cache.popitem(last=False)
        d=self.cache[uid];start=d['starts'][u.ordinal]
        mask=(d['frame_times']>=start-.04)&(d['frame_times']<=d['ends'][u.ordinal]+.04)
        return dict(phones=d['phones'][u.ordinal],durations=d['durations'][u.ordinal],
            character=d['characters'][u.ordinal],
            log_f0=d['log_f0'][u.ordinal],voiced=d['voiced'][u.ordinal],
            frame_times=d['frame_times'][mask]-start,frame_f0=d['frame_f0'][mask],
            frame_voiced=d['frame_voiced'][mask])


class ProgramCosts:
    parameters=0
    def __init__(self,inventory,store,plan,max_pitch=12.,duration_bounds=(.5,2.)):
        self.inventory=inventory;self.store=store;self.plan=plan
        self.max_pitch=max_pitch;self.duration_bounds=duration_bounds;self.details={}

    def costs(self,inventory,tokens,position,unused):
        values={};details={}
        for u in inventory.by_token[tokens[position]]:
            a=self.store.unit(u)
            try:source,target,goal,voiced=desired_for(a,self.plan,position)
            except ValueError:
                values[u.uid]=float('inf');continue
            mask=(a['voiced']>0)&(voiced>=.4)
            if mask.sum()<3:
                values[u.uid]=float('inf');continue
            correction=(goal-a['log_f0'])*ST
            required=abs(correction[mask]);ratios=target/source
            feasible=(required.max()<=self.max_pitch and ratios.min()>=self.duration_bounds[0] and ratios.max()<=self.duration_bounds[1])
            mean=float(required.mean());shape=float(np.mean(abs(correction[mask]-np.median(correction[mask]))))
            duration=float(np.mean(abs(np.log(ratios))))
            context=inventory.source_context(u)
            left=tokens[position-1] if position else None
            right=tokens[position+1] if position+1<len(tokens) else None
            penalty=.06*(context.previous!=left)+.06*(context.following!=right)
            score=.10*mean+.10*shape+.25*duration+penalty
            values[u.uid]=float(score) if feasible else float('inf')
            details[u.uid]=dict(unit_id=u.uid,token=u.token,source_origin=inventory.sources[u.utterance].get('origin',u.utterance),
                source_character=str(a.get('character','')),
                original_context=vars(context),source_phone_durations=source.tolist(),target_phone_durations=target.tolist(),
                duration_ratios=ratios.tolist(),mean_pitch_correction_st=mean,max_pitch_correction_st=float(required.max()),
                contour_change_st=shape,feasible=bool(feasible),source_f0=np.where(a['voiced']>0,np.exp(a['log_f0']),0).tolist(),
                target_f0_on_source_knots=np.exp(goal).tolist(),comparison_mask=mask.tolist(),cost=float(score))
        if not any(np.isfinite(x) for x in values.values()):
            raise ValueError('No source meets actual pitch/duration target range for '+tokens[position])
        self.details[position]=details
        return values

    def select(self,tokens):
        previous=self.inventory.selector;self.inventory.selector=self
        try:spans,cost=self.inventory.select(tokens)
        finally:self.inventory.selector=previous
        if not np.isfinite(cost):raise ValueError('No finite, target-feasible source path')
        records=[]
        for span in spans:
            for offset,u in enumerate(self.inventory.units[span.utterance][span.first:span.last+1]):
                records.append(self.details[span.target_start+offset][u.uid])
        return spans,records


def track(audio,rate):
    if len(audio)<round(rate*.045):return np.array([]),np.array([]),np.array([])
    sound=parselmouth.Sound(audio.astype(float)/32768,rate)
    p=sound.to_pitch_ac(time_step=.005,pitch_floor=75,pitch_ceiling=600,voicing_threshold=.5)
    return p.xs(),p.selected_array['frequency'],p.selected_array['strength']


def inspect_output(audio,rate,positions,plan):
    times,hz,strength=track(audio,rate);records=[]
    for item in positions:
        index=item['target_index'];start=item['output_start']/rate;end=item['output_end']/rate
        query=start+KNOTS*(end-start)
        expected=np.array(plan['f0'][index]);expected_mask=np.array(plan['voiced'][index])>=.4
        if not len(times):
            records.append(dict(target_index=index,token=plan['tokens'][index],unit_id=item['unit_id'],
                target_duration_seconds=float(sum(plan['phone_durations'][index])),actual_duration_seconds=end-start,
                target_f0=expected.tolist(),actual_f0=[0.]*24,comparison_mask=[False]*24,
                mean_f0_error_st=None,p95_f0_error_st=None,voiced_target_coverage=0.,
                target_tracking_pass=False,lexical_tone_correctness_verified=False))
            continue
        nearest=np.searchsorted(times,query).clip(0,len(times)-1)
        prior=np.maximum(nearest-1,0)
        nearest=np.where(abs(times[prior]-query)<abs(times[nearest]-query),prior,nearest)
        valid=(abs(times[nearest]-query)<.008)&(hz[nearest]>0)&(strength[nearest]>=.6)&expected_mask
        actual=hz[nearest]
        error=(np.log(np.maximum(actual,75))-np.log(expected))*ST
        mean=float(np.mean(abs(error[valid]))) if valid.any() else None
        records.append(dict(target_index=index,token=plan['tokens'][index],unit_id=item['unit_id'],
            target_duration_seconds=float(sum(plan['phone_durations'][index])),
            actual_duration_seconds=end-start,target_f0=expected.tolist(),actual_f0=np.where(valid,actual,0).tolist(),
            comparison_mask=valid.tolist(),mean_f0_error_st=mean,
            p95_f0_error_st=float(np.percentile(abs(error[valid]),95)) if valid.any() else None,
            voiced_target_coverage=float(valid.sum()/max(expected_mask.sum(),1)),
            target_tracking_pass=bool(valid.sum()>=4 and len(np.unique(nearest[valid]))>=3 and
                                      valid.sum()>=.65*expected_mask.sum() and mean<.75),
            lexical_tone_correctness_verified=False))
    return records


def render_span(inventory,span,store,plan,refinements=2,return_context=False):
    sequence=inventory.units[span.utterance];first,last=sequence[span.first],sequence[span.last]
    original,rate=inventory.wave(span.utterance);pad=round(.04*rate)
    a=max(0,first.start-pad);b=min(len(original),last.end+pad);patch=original[a:b].copy();duration=len(patch)/rate
    intervals=[];all_t=[];all_f=[];units=[]
    for offset,u in enumerate(sequence[span.first:span.last+1]):
        index=span.target_start+offset;data=store.unit(u);source,target,goal,voiced=desired_for(data,plan,index)
        ratios=target/source
        if ratios.min()<.5 or ratios.max()>2.:raise ValueError('Selected phone duration outside direct-render range')
        start=(u.start-a)/rate;end=(u.end-a)/rate;cursor=start
        for d,f in zip(source,ratios):intervals.append((cursor,cursor+d,f));cursor+=d
        t=data['frame_times'].astype(float)+start;f=data['frame_f0'].astype(float)
        mask=(t>=0)&(t<=duration)&(f>0)&(data['frame_voiced']>0)
        all_t.extend(t[mask]);all_f.extend(f[mask]);units.append((u,index,start,end,source,target))
    dt,df=duration_tier(intervals,duration)
    order=np.argsort(all_t);pt=np.array(all_t)[order];source_hz=np.array(all_f)[order]
    _,unique=np.unique(np.round(pt,6),return_index=True);pt=pt[unique];source_hz=source_hz[unique]
    if len(pt)<3:raise ValueError('No reliable voiced context for selected span')
    requested=source_hz.copy();goal_mask=np.zeros(len(pt),bool)
    for u,index,start,end,source,target in units:
        mask=(pt>=start)&(pt<=end)
        relative=phase_time(pt[mask]-start,source,target)/target.sum()
        voiced=np.interp(relative,KNOTS,plan['voiced'][index])>=.4
        requested[mask]=np.where(voiced,np.exp(np.interp(relative,KNOTS,np.log(plan['f0'][index]))),requested[mask])
        goal_mask[mask]=voiced
    if np.any((requested<75)|(requested>600)) or not np.isfinite(requested).all():
        raise ValueError('Global F0 target outside declared analysis/render range')
    crop_start=round(map_time((first.start-a)/rate,dt,df)*rate)
    crop_end=round(map_time((last.end-a)/rate,dt,df)*rate)
    positions=[dict(target_index=index,unit_id=u.uid,
        output_start=round(map_time(start,dt,df)*rate)-crop_start,
        output_end=round(map_time(end,dt,df)*rate)-crop_start) for u,index,start,end,_,_ in units]
    output_times=np.array([map_time(t,dt,df) for t in pt])-crop_start/rate
    tier=requested.copy();best=None;best_error=float('inf');attempts=[]
    for attempt in range(refinements+1):
        # Each pass starts from the original PCM, never from a resynthesized pass.
        try:
            rendered,detail=render_controls(patch,rate,pt,tier,dt,df,
                pitch_limit_semitones=12.,duration_limits=(.5,2.),clip_pitch=False)
        except ValueError as exc:
            if best is None:raise
            attempts.append(dict(attempt=attempt,rejected=str(exc)));break
        if crop_end>len(rendered)+3:raise ValueError('Direct-render time map inconsistent')
        audio=rendered[crop_start:min(crop_end,len(rendered))]
        records=inspect_output(audio,rate,positions,plan)
        measured=[r['mean_f0_error_st'] for r in records if r['mean_f0_error_st'] is not None]
        error=float(np.mean(measured)) if measured else float('inf')
        attempts.append(dict(attempt=attempt,mean_unit_f0_error_st=error,units=records))
        if best is None or error<best_error:
            best=(audio.copy(),detail,records,attempt,rendered.copy() if return_context else None);best_error=error
        if error<.35 or attempt==refinements:break
        times,hz,strength=track(audio,rate);valid=(hz>0)&(strength>=.6)
        if valid.sum()<3:break
        actual=np.interp(output_times,times[valid],hz[valid])
        correction=np.clip(np.log(requested/actual),-np.log(2)/12,np.log(2)/12)
        support=goal_mask&(output_times>=0)&(output_times<=len(audio)/rate)
        tier[support]*=np.exp(correction[support]);tier=np.clip(tier,75,600)
    if best is None:raise ValueError('Direct renderer produced no measurable candidate')
    audio,detail,records,attempt,context_audio=best
    result=(audio,rate,positions,dict(span=vars(span),render=detail,chosen_refinement=attempt,
        actual_goal_checks=records,attempts=attempts,mean_unit_f0_error_st=best_error)
    )
    if return_context:
        return (*result,dict(audio=context_audio,start=crop_start,end=crop_start+len(audio)))
    return result


def render_phrase(inventory,spans,store,plan):
    result=None;positions=[];span_records=[];joins=[];previous=None
    for span in spans:
        audio,rate,local,record=render_span(inventory,span,store,plan)
        if result is None:offset=0;result=audio
        else:
            # Use F0 at the actual processed boundary, not a whole-syllable source median.
            _,af,_=track(result[-min(len(result),round(rate*.08)):],rate)
            _,bf,_=track(audio[:min(len(audio),round(rate*.08))],rate)
            left=float(np.median(af[af>0])) if np.any(af>0) else 0.
            right=float(np.median(bf[bf>0])) if np.any(bf>0) else 0.
            before=len(result);result,join=bounded_join(result,audio,rate,left,right)
            offset=before-join['removed_samples'];joins.append(join)
        for p in local:positions.append({**p,'output_start':p['output_start']+offset,'output_end':p['output_end']+offset})
        span_records.append(record);previous=span
    measured=inspect_output(result,rate,positions,plan)
    return result,rate,dict(spans=span_records,joins=joins,units_after_join=measured,
                           direct_goal_renderer=True,neural_adjuster=False)


def raw_phrase(inventory,spans,plan):
    from .synthesize import render
    audio,rate,joins=render(inventory,spans);positions=[];offset=0
    for si,span in enumerate(spans):
        sequence=inventory.units[span.utterance];first,last=sequence[span.first],sequence[span.last]
        if si:offset-=joins[si-1]['removed_samples']
        for j,u in enumerate(sequence[span.first:span.last+1]):
            positions.append(dict(target_index=span.target_start+j,unit_id=u.uid,
                                  output_start=offset+u.start-first.start,output_end=offset+u.end-first.start))
        offset+=last.end-first.start
    return audio,rate,dict(joins=joins,units_after_join=inspect_output(audio,rate,positions,plan))
