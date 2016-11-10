/*
 *  Prolog part of plsmf: standard MIDI file reading library
 *
 *  Copyright (C) 2009-2015 Samer Abdallah (Queen Mary University of London; UCL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
	  
:- module(plsmf,
	[	smf_new/2
   ,  smf_delete/1
   ,  smf_read/2		
   ,  smf_write/2
	,	smf_description/2
	,	smf_duration/2
	,	smf_duration/3
	,	smf_events/2
	,	smf_events/3
	,	smf_events/4
	,	smf_events_between/4
   ,  smf_add_events/2
   ,  smf_add_events/3
   ,  smf_property/2
   ,  smf_tempo/3
	,	is_smf/1	
	]).
	
/** <module> Standard MIDI file reading and writing

Types used below:
==
smf_event ---> msg(nonneg, byte)
             ; msg(nonneg, byte, byte)
             ; msg(nonneg, byte, byte, byte)
             ; meta(nonneg, byte, byte, list(byte)).

timeline ---> physical  % time in seconds
            ; metrical. % time in quarter notes

==

@author Samer Abdallah
*/

:-	use_foreign_library(foreign(plsmf)).


%% smf_new(+PPQN:natural, -Ref:smf_blob) is det.
%  Creats a new, empty SMF blob. Data structures will be released
%  when the blob is garbage collected, or when smf_delete/1 is called.

%% smf_delete(+Ref:smf_blob) is det.
%  Release data structures associated with an SMF blob.

%% smf_read( +File:filename, -Ref:smf_blob) is semidet.
%
%  Attempts to read standard MIDI file named File and sets Ref
%  to an SMF blob atom which can be used to make further queries
%  about the file.

%% smf_write(+Ref:smf_blob, +File:filename) is det.
%  Writes a MIDI file.


%% smf_duration( +Ref:smf_blob, +Timeline:oneof([metrical,physical]), -Dur:nonneg) is det.
%% smf_duration( +Ref:smf_blob, -Dur:nonneg) is det.
%
%  Returns the duration of the MIDI file in seconds (Timeline=physical) or
%  pulses (Timeline=metrical).
smf_duration(Ref,Dur) :- smf_duration(Ref,physical,Dur).

%% smf_description( +Ref:smf_blob, -Desc:atom) is det.
%
%  Sets Desc to an atom containing descriptive text about the
%  MIDI file, inluding the number of tracks and timing information.

%% smf_events( +Ref:smf_blob, -Events:list(smf_event)) is det.
%% smf_events( +Ref:smf_blob, +TL:timeline, -Events:list(smf_event)) is det.
%% smf_events( +Ref:smf_blob, -TL:timeline, -Events:list(smf_event)) is multi.
%% smf_events( +Ref:smf_blob, Tracks:track_spec, +TL:timeline, -Events:list(smf_event)) is det.
%
%  Unifies Events with a list containing events in the MIDI file.
%  Not all types of events are handled, but most are. Events are
%  returned in a low-level numeric format containing the bytes
%  in the original MIDI data. See module header for definition of
%  type =|smf_event|=.
%  ==
%  track_spec ---> all; track(nat).
%  ==
%
%  
%  @see smf_events_between/4.
smf_events(Ref,Events) :- smf_events(Ref,physical,Events).
smf_events(Ref,Timeline,Events) :- smf_events(Ref,all,Timeline,Events).

smf_events(Ref,all,Timeline,Events) :- 
   timeline(Timeline),
   smf_events_without_track(Ref,0,Timeline,-1,-1,Events1),
   (  Timeline=physical -> Events=Events1
   ;  smf_info(Ref,ppqn,PPQN),
      maplist(to_beats(PPQN),Events1,Events)
   ).
smf_events(Ref,track(T),Timeline,Events) :- 
   timeline(Timeline),
   smf_property(Ref,tracks(N)), between(1,N,T),
   smf_events_without_track(Ref,T,Timeline,-1,-1,Events1),
   (  Timeline=physical -> Events=Events1
   ;  smf_info(Ref,ppqn,PPQN),
      maplist(to_beats(PPQN),Events1,Events)
   ).

to_beats(PPQN,msg(X,A,B),msg(Y,A,B)) :- Y is X rdiv PPQN.
to_beats(PPQN,msg(X,A,B,C),msg(Y,A,B,C)) :- Y is X rdiv PPQN.
to_beats(PPQN,msg(X,A,B,C,D),msg(Y,A,B,C,D)) :- Y is X rdiv PPQN.
to_beats(PPQN,meta(X,A,B,C),meta(Y,A,B,C)) :- Y is X rdiv PPQN.

%% smf_events_between( +Ref:smf_blob, +T1:nonneg, +T2:nonneg, -Events:list(smf_event)) is det.
%
%  Unifies Events with a list containing events in the MIDI file
%  between the given times T1 and T2. See smf_events/2 for more
%  information about the format of the events list.
smf_events_between(Ref,T1,T2,Events) :-
   smf_events_without_track(Ref,0,physical,T1,T2,Events).

%% is_smf(+Ref) is semidet.
%
%  Determines whether or not Ref is a MIDI file BLOB as returned
%  by smf_read/2.


%% smf_property(+Ref:smf_blob, ?Prop:smf_prop) is multi.
%  Queries properties of a MIDI score:
%  ==
%  smf_prop ---> ppqn(nat)   % pulses per quarter note
%              ; fps(nat)    % frames per second
%              ; tracks(nat) % number of tracks
%              ; resolution(number).
%  ===
smf_property(Ref,Prop) :-
   member(Key, [ppqn, fps, tracks, resolution]),
   Prop =.. [Key,Val],
   smf_info(Ref,Key,Val).


%% smf_tempo(+Ref:smf_blob, +T:time_spec, -P:tempo_spec) is nondet.
%  Gets the tempo at the given time in a variety of forms.
%  ==
%  time_spec ---> seconds(nonneg); pulses(nat).
%  tempo_spec ---> time(timeline, number)
%                ; crochet_duration(nonneg)
%                ; crochets_per_minute(nonneg)
%                ; time_signature(timesig).
%  timesig ---> nat/nat.
%  ==
smf_tempo(Ref,seconds(T),Prop) :- smf_tempo(Ref,physical,T,Tempo), tempo_property(Tempo,Prop).
smf_tempo(Ref,pulses(T),Prop)  :- smf_tempo(Ref,metrical,T,Tempo), tempo_property(Tempo,Prop).

tempo_property(smf_tempo(T,_,_,_,_,_,_),time(metrical,T)).
tempo_property(smf_tempo(_,T,_,_,_,_,_),time(physical,T)).
tempo_property(smf_tempo(_,_,N,_,_,_,_),crochet_duration(D)) :- D is N rdiv 1000000.
tempo_property(smf_tempo(_,_,N,_,_,_,_),crochets_per_minute(D)) :- D is 60000000 rdiv N.
tempo_property(smf_tempo(_,_,_,N,D,_,_),time_signature(N/D)).

timeline(metrical).
timeline(physical).


%% smf_add_events(+Ref:smf_blob, +TL:timeline, +Events:list(smf_event)) is det.
%% smf_add_events(+Ref:smf_blob, +Events:list(smf_event)) is det.
%
%  Adds a new track containing the given events.
smf_add_events(SMF, Events) :- smf_add_events(SMF, physical, Events).
