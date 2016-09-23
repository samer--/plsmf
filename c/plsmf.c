/*
 *  plsmf - Standard MIDI file reading and writing for SWI Prolog
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
 *
 */


#include <SWI-Stream.h>
#include <SWI-Prolog.h>

#include <smf.h>

#include <stdio.h>
#include <string.h>

typedef struct smf_blob {
	smf_t *smf;
} smf_blob_t;

static PL_blob_t smf_blob;
static functor_t f_midi[5], f_tempo;
static atom_t a_physical, a_metrical;

#define TL_METRICAL 0
#define TL_PHYSICAL 1

// --------------------------   Prolog boilerplate

install_t install();

foreign_t open_read( term_t filename, term_t smf); 
foreign_t write_smf( term_t smf, term_t filename); 
foreign_t new_smf( term_t smf); 
foreign_t delete_smf( term_t smf); 
foreign_t is_smf( term_t conn); 
foreign_t get_info( term_t smf, term_t key, term_t val);
foreign_t get_description( term_t smf, term_t desc);
foreign_t get_duration( term_t smf, term_t timeline, term_t dur);
foreign_t get_events_with_track( term_t smf, term_t trackno, term_t timeline, term_t t1, term_t t2, term_t events);
foreign_t get_events_without_track( term_t smf, term_t trackno, term_t timeline, term_t t1, term_t t2, term_t events);
foreign_t add_events( term_t smf, term_t timeline, term_t events);
foreign_t get_tempo( term_t smf, term_t timeline, term_t time, term_t tempo);

int smf_release(atom_t a)
{
	PL_blob_t *type;
	size_t    len;
	smf_blob_t *p;

	p=(smf_blob_t *)PL_blob_data(a,&len,&type);
	if (p && p->smf) smf_delete(p->smf);
	return TRUE;
}

install_t install() 
{ 
	PL_register_foreign("smf_new",  1, (void *)new_smf, 0);
	PL_register_foreign("smf_delete", 1, (void *)delete_smf, 0);
	PL_register_foreign("smf_read",  2, (void *)open_read, 0);
	PL_register_foreign("smf_write",  2, (void *)write_smf, 0);
	PL_register_foreign("smf_info", 3, (void *)get_info, 0);
	PL_register_foreign("smf_description", 2, (void *)get_description, 0);
	PL_register_foreign("smf_duration", 3, (void *)get_duration, 0);
	PL_register_foreign("smf_events_with_track", 6, (void *)get_events_with_track, 0);
	PL_register_foreign("smf_events_without_track", 6, (void *)get_events_without_track, 0);
	PL_register_foreign("smf_add_events", 3, (void *)add_events, 0);
	PL_register_foreign("smf_tempo", 4, (void *)get_tempo, 0);
	PL_register_foreign("is_smf",  1, (void *)is_smf, 0);

	a_physical = PL_new_atom("physical");
	a_metrical = PL_new_atom("metrical");

	{
		atom_t a_midi = PL_new_atom("smf");
		atom_t a_tempo = PL_new_atom("smf_tempo");
		int i;
		for (i=0; i<5; i++) f_midi[i] = PL_new_functor(a_midi,1+i);
		f_tempo = PL_new_functor(a_tempo,7);
	}

	smf_blob.magic = PL_BLOB_MAGIC;
	smf_blob.flags = PL_BLOB_UNIQUE;
	smf_blob.name = "plsmf_smf";
	smf_blob.acquire = 0; // rs_acquire;
	smf_blob.release = smf_release; 
	smf_blob.compare = 0; // rs_compare;
	smf_blob.write   = 0; // rs_write;
}

static int io_error(const char *file, const char *action)
{ 
	term_t ex = PL_new_term_ref();

  return PL_unify_term(ex, PL_FUNCTOR_CHARS, "error", 2,
								PL_FUNCTOR_CHARS, "smf_error", 2,
								  PL_CHARS, file,
								  PL_CHARS, action,
								PL_VARIABLE)
      && PL_raise_exception(ex);
}

static int smf_error(const char *msg)
{ 
	term_t ex = PL_new_term_ref();

  return PL_unify_term(ex, PL_FUNCTOR_CHARS, "error", 2,
								PL_FUNCTOR_CHARS, "smf_error", 1,
								  PL_CHARS, msg,
								PL_VARIABLE)
	   && PL_raise_exception(ex);
}
	
// get an unsigned byte from a numeric atom
static int get_byte(term_t msg, unsigned char *m)
{
   int x;
   if (!PL_get_integer(msg,&x) || x<0 || x>255) return PL_type_error("uint8",msg);
   *m = x;
   return TRUE;
}

static int unify_smf(term_t smf,smf_blob_t *p) {
	return PL_unify_blob(smf, p, sizeof(smf_blob_t), &smf_blob); 
}

static int get_smf(term_t smf, smf_blob_t *p)
{ 
	PL_blob_t *type;
	size_t    len;
	smf_blob_t *p1;
  
	PL_get_blob(smf, (void **)&p1, &len, &type);
	if (type != &smf_blob) { return PL_type_error("smf_blob",smf); }
	else { *p=*p1; return TRUE; }
} 

int chomp(const unsigned char status, unsigned short *size) 
{
	// We are expecting that the next byte in the packet is a status byte.
	if (!(status & 0x80)) {*size=0; return 1;} // abort this packet

	// event types:
	// FF     metadata
	// F8-FE  system_realtime IGNORE
	// F0-F7  system_common   IGNORE
	// F0     sysex           IGNORE
	// end of track
	// textual
	//
	// Determine the number of bytes in the MIDI message.
	if      (status<0xC0) *size=3; 
	else if (status<0xE0) *size=2;
	else if (status<0xF0) *size=3;
	else {
		switch (status) {
			case 0xF0: *size=0; return 1; // sys ex: ignore
			case 0xF1: *size=3; return 1; // time code: ignore
			case 0xF2: *size=3; break;
			case 0xF3: *size=2; break;
			default:   *size=1; 
		}
	}
	return 0; // don't ignore this packet
}

// -------------------------------------------------------------------------------------------


typedef struct event_source {
	smf_event_t *(*next)(struct event_source *);
	union { smf_t *smf; smf_track_t *track; } src;
} event_source_t;

smf_event_t *smf_next_event(event_source_t *src) { 
	return smf_get_next_event(src->src.smf);
}

smf_event_t *track_next_event(event_source_t *src) { 
	return smf_track_get_next_event(src->src.track);
}

typedef int (*put_time_t)(smf_event_t *, term_t);
typedef int (*unify_event_t)(put_time_t, smf_event_t *, unsigned short, term_t);

typedef union midi_time {
	double seconds;
	int   pulses;
} midi_time_t;

typedef struct time_spec {
	int (*test_time)(struct time_spec *me, smf_event_t *ev);
	put_time_t put_time;
	midi_time_t tmax;
} time_spec_t;

typedef int (*init_ts_t)(smf_t *, term_t, term_t, time_spec_t *);

// -- time in seconds ------------------------
static int no_test_time(time_spec_t *me, smf_event_t *ev) { return FALSE; }

static int test_time_seconds(time_spec_t *me, smf_event_t *ev) {
	return (ev->time_seconds > me->tmax.seconds);
}

static int put_time_seconds(smf_event_t *ev, term_t t) {
	return PL_put_float(t,ev->time_seconds);
}

static int physical_time_spec(smf_t *smf, term_t tmin, term_t tmax, time_spec_t *ts) {
	double t1, t2;

	if (!PL_get_float_ex(tmin,&t1) || !PL_get_float_ex(tmax,&t2)) return FALSE;
	if (t1>0.0) { if (smf_seek_to_seconds(smf,t1)) return FALSE; } else { smf_rewind(smf); }
	if (t2<0.0) {
		ts->test_time = no_test_time;
	} else {
		ts->test_time = test_time_seconds;
		ts->tmax.seconds = t2;
	}
	ts->put_time = put_time_seconds;
	return TRUE;
}

// -- time in pulses ------------------------
static int test_time_pulses(time_spec_t *me, smf_event_t *ev) {
	return (ev->time_pulses > me->tmax.pulses);
}

static int put_time_pulses(smf_event_t *ev, term_t t) {
	return PL_put_integer(t,ev->time_pulses);
}

static int metrical_time_spec(smf_t *smf, term_t tmin, term_t tmax, time_spec_t *ts) {
	int t1, t2;

	if (!PL_get_integer_ex(tmin,&t1) || !PL_get_integer_ex(tmax,&t2)) return FALSE;
	if (t1>0) { if (smf_seek_to_pulses(smf,t1)) return FALSE; } else smf_rewind(smf);
	if (t2<0) {
		ts->test_time = no_test_time;
	} else {
		ts->test_time = test_time_pulses;
		ts->tmax.pulses = t2;
	}
	ts->put_time = put_time_pulses;
	return TRUE;
}

static int unify_event_without_track(put_time_t put_time, smf_event_t *ev, unsigned short size, term_t event)
{
	term_t data0=PL_new_term_refs(1+size);
	int i;

	for (i=0; i<size; i++) {
		if (!PL_put_integer(data0+i+1,ev->midi_buffer[i])) return FALSE;
	}
	return put_time(ev,data0)
		 && PL_cons_functor_v(event,f_midi[size],data0);
}

static int unify_event_with_track(put_time_t put_time, smf_event_t *ev, unsigned short size, term_t event)
{
	term_t data0=PL_new_term_refs(2+size);
	int i;

	for (i=0; i<size; i++) {
		if (!PL_put_integer(data0+i+2,ev->midi_buffer[i])) return FALSE;
	}
	return put_time(ev,data0)
 	 	 && PL_put_integer(data0+1,ev->track_number)
		 && PL_cons_functor_v(event,f_midi[1+size],data0);
}

static int read_events(event_source_t *src, unify_event_t unify_event, time_spec_t *ts, term_t events)
{
	/* term_t event=PL_new_term_ref(); */
	smf_event_t	*ev;
	term_t head = PL_new_term_ref();
	term_t event = PL_new_term_ref();

	events=PL_copy_term_ref(events);
	while ((ev=src->next(src)) != NULL) {

		if (smf_event_is_metadata(ev)) continue; 
		if (ts->test_time(ts,ev)) break;

		unsigned short size;
		if (chomp(ev->midi_buffer[0],&size)) continue;

		if (	!unify_event(ts->put_time, ev, size, event)
			||	!PL_unify_list(events,head,events) 
			|| !PL_unify(head,event)
			) return FALSE;
	}
	return PL_unify_nil(events);
}

static int add_event_seconds(smf_track_t *track, smf_event_t *event, term_t time) {
	double t;
	if (!PL_get_float(time,&t)) return FALSE;
	smf_track_add_event_seconds(track,event,t);
	return TRUE;
}

static int add_event_pulses(smf_track_t *track, smf_event_t *event, term_t time) {
	int t;
	if (!PL_get_integer(time,&t)) return FALSE;
	smf_track_add_event_pulses(track,event,t);
	return TRUE;
}

static int add_events_to_track(term_t events, int tl, smf_track_t *track)
{
	term_t head=PL_new_term_ref();
	unsigned char msg, arg1, arg2;
	int (*add_event_at)(smf_track_t *, smf_event_t *, term_t);
	
	add_event_at = tl==TL_PHYSICAL ? add_event_seconds : add_event_pulses;
	events = PL_copy_term_ref(events);
	while (PL_get_list(events,head,events)) {
      atom_t name;
      int    arity;

      if (PL_get_name_arity(head,&name,&arity) && arity==4 && !strcmp(PL_atom_chars(name),"smf")) {
			term_t args=PL_new_term_refs(4);

			if (   PL_get_arg(1,head,args+0) 
             && PL_get_arg(2,head,args+1) && get_byte(args+1,&msg)
             && PL_get_arg(3,head,args+2) && get_byte(args+2,&arg1)
             && PL_get_arg(4,head,args+3) && get_byte(args+3,&arg2) ) {
				smf_event_t *event=smf_event_new_from_bytes(msg,arg1,arg2);
				if (event==NULL) return smf_error("smf_event_new_from_bytes");
				if (!add_event_at(track,event,args+0)) return smf_error("time spec");
			} else return smf_error("midi event");
		} else return PL_type_error("midi/4",head);
	}
	return TRUE;
}

static int get_timeline(term_t timeline, int *x) {
	atom_t tl;
	if (!PL_get_atom_ex(timeline,&tl)) return FALSE;
	if (tl==a_physical)      *x = TL_PHYSICAL;
	else if (tl==a_metrical) *x = TL_METRICAL;
	else                     return PL_domain_error("metrical or physical",timeline);
	return TRUE;
}
// ------- Foreign interface predicates

foreign_t new_smf(term_t smf) 
{ 
	smf_blob_t	smfb;

	smfb.smf = smf_new();
	if (smfb.smf) return unify_smf(smf,&smfb);
	else return smf_error("smf_new"); 
}

foreign_t delete_smf(term_t smf) 
{ 
	PL_blob_t *type;
	size_t    len;
	smf_blob_t *p1;
  
	PL_get_blob(smf, (void **)&p1, &len, &type);
	if (type != &smf_blob) { return PL_type_error("smf_blob",smf); }
	smf_delete(p1->smf);
	p1->smf=0;
	return TRUE;
}

foreign_t open_read(term_t filename, term_t smf) 
{ 
	char 			*fn;
	smf_blob_t	smfb;

	if (PL_get_chars(filename, &fn, CVT_ATOM | CVT_STRING | REP_UTF8)) {
		smfb.smf = smf_load(fn);
		if (smfb.smf) return unify_smf(smf,&smfb);
		else return io_error(fn,"read"); 
	} else return PL_type_error("text",filename);
}

foreign_t write_smf(term_t smf, term_t filename) 
{ 
	char 			*fn;
	smf_blob_t	s;

	return PL_get_chars(filename, &fn, CVT_ATOM | CVT_STRING | REP_UTF8)
       && get_smf(smf,&s)
		 && (smf_save(s.smf,fn) ? io_error(fn,"write") : TRUE);
}

foreign_t get_description( term_t smf, term_t desc)
{
	smf_blob_t	s;
	
	if (get_smf(smf,&s)) {
		char 	*d=smf_decode(s.smf);
		if (d) {
			int rc=PL_unify_atom_chars(desc,d);
			free(d);
			return rc;
		} else return FALSE;
	} return FALSE;
}

foreign_t get_info(term_t smf, term_t key, term_t val)
{
	smf_blob_t s;
	char *k;
	int  v;
	if (!get_smf(smf,&s) || !PL_get_atom_chars(key, &k)) return FALSE;
	if      (!strcmp(k,"ppqn"))       v=s.smf->ppqn;
	else if (!strcmp(k,"fps"))        v=s.smf->frames_per_second;
	else if (!strcmp(k,"tracks"))     v=s.smf->number_of_tracks;
	else if (!strcmp(k,"resolution")) v=s.smf->resolution;
	else return PL_domain_error("Unrecognised SMF information key",key);
	return PL_unify_integer(val, v);
}

foreign_t get_duration( term_t smf, term_t timeline, term_t dur)
{
	smf_blob_t	s;
	int tl;

	return get_smf(smf,&s)
		 && get_timeline(timeline,&tl)
       && (tl==TL_PHYSICAL ? PL_unify_float(dur,smf_get_length_seconds(s.smf))
                           : PL_unify_integer(dur,smf_get_length_pulses(s.smf)));
}

static int unify_tempo(term_t tempo, smf_tempo_t *t)
{
	term_t args=PL_new_term_refs(8);

	return PL_put_integer(args+1, t->time_pulses)
		 && PL_put_float(args+2, t->time_seconds)
		 && PL_put_integer(args+3, t->microseconds_per_quarter_note)
		 && PL_put_integer(args+4, t->numerator)
		 && PL_put_integer(args+5, t->denominator)
		 && PL_put_integer(args+6, t->clocks_per_click)
		 && PL_put_integer(args+7, t->notes_per_note)
		 && PL_cons_functor_v(args,f_tempo,args+1)
		 && PL_unify(tempo,args);
}

foreign_t get_tempo( term_t smf, term_t timeline, term_t time, term_t tempo)
{
	smf_blob_t	s;
	midi_time_t t;
	int tl;

	return get_smf(smf,&s)
		 && get_timeline(timeline,&tl)
       && (tl==TL_PHYSICAL ? ( PL_get_float(time, &t.seconds)
					                && unify_tempo(tempo,smf_get_tempo_by_seconds(s.smf,t.seconds)))
                           : ( PL_get_integer(time, &t.pulses)
										 && unify_tempo(tempo,smf_get_tempo_by_pulses(s.smf,t.pulses))));
}


static int get_events( unify_event_t unify_event, term_t smf, 
							  term_t track_no, term_t timeline, term_t start, term_t end, 
							  term_t events)
{
	smf_blob_t	   s;
	int			   tno;
	event_source_t src;
	time_spec_t    ts;
	int            tl;

	if (  !get_smf(smf,&s) 
		|| !PL_get_integer_ex(track_no,&tno)
		|| !get_timeline(timeline,&tl)
		|| !(tl==TL_PHYSICAL ? physical_time_spec(s.smf,start,end,&ts)
		                     : metrical_time_spec(s.smf,start,end,&ts))
		) return FALSE;

	if (tno==0) {
		src.next=smf_next_event;
		src.src.smf=s.smf;
	} else {
		src.next=track_next_event;
		src.src.track=smf_get_track_by_number(s.smf,tno);
		if (src.src.track==NULL) return FALSE;
	}

	return read_events(&src, unify_event, &ts, events);
}

foreign_t get_events_without_track( term_t smf, term_t tno, term_t tl, term_t start, term_t end, term_t events) {
	return get_events(unify_event_without_track, smf,tno, tl, start, end, events);
}

foreign_t get_events_with_track( term_t smf, term_t tno, term_t tl, term_t start, term_t end, term_t events) {
	return get_events(unify_event_with_track, smf, tno, tl, start, end, events);
}

foreign_t add_events( term_t smf, term_t timeline, term_t events)
{
	smf_blob_t	s;
	smf_track_t *track;
	int            tl;

	if (!get_smf(smf,&s)) return FALSE;
	if	(!get_timeline(timeline,&tl)) return FALSE;
	track = smf_track_new();
	if (track==NULL) return smf_error("smf_track_new");
	smf_add_track(s.smf,track);
	return add_events_to_track(events,tl,track);
}

foreign_t is_smf(term_t conn) 
{ 
	PL_blob_t *type;
	return PL_is_blob(conn,&type) && type==&smf_blob;
}

