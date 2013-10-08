/*  Copyright 2013, JP Norair
  *
  * Licensed under the OpenTag License, Version 1.0 (the "License");
  * you may not use this file except in compliance with the License.
  * You may obtain a copy of the License at
  *
  * http://www.indigresso.com/wiki/doku.php?id=opentag:license_1_0
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  */
/**
  * @file       /otlib/alp_main.c
  * @author     JP Norair
  * @version    R102
  * @date       26 Sept 2013
  * @brief      Application Layer Protocol Main Processor
  * @ingroup    ALP
  *
  * ALP is a library, not a task!
  *
  * The functions implemented in this file are for "main ALP."  Individual
  * protocol processing routines are implemented in other files named alp...c,
  * which should be in the same directory as this alp_main.c.
  *
  ******************************************************************************
  */

#include "OTAPI.h"
#include "alp.h"
#include "ndef.h"
#include "OT_platform.h"

#if ((OT_FEATURE(ALP) == ENABLED) && (OT_FEATURE(SERVER) == ENABLED))

#define ALP_FILESYSTEM  1
#define ALP_SENSORS     (OT_FEATURE(SENSORS) == ENABLED)
#define ALP_SECURITY    ((OT_FEATURE(NL_SECURITY) == ENABLED) || (OT_FEATURE(DLL_SECURITY) == ENABLED))
#define ALP_LOGGER      (LOG_FEATURE(ANY) == ENABLED)
#define ALP_DASHFORTH   (OT_FEATURE(DASHFORTH) == ENABLED)
#define ALP_API         ((OT_FEATURE(ALPAPI) == ENABLED)*3)
#define ALP_EXT         (OT_FEATURE(ALPEXT) == ENABLED)


#define ALP_MAX         9
#define ALP_FUNCTIONS   (   ALP_FILESYSTEM \
                          + ALP_SENSORS \
                          + ALP_SECURITY \
                          + ALP_LOGGER \
                          + ALP_DASHFORTH \
                          + ALP_API )


typedef ot_bool (*sub_proc)(alp_tmpl*, id_tmpl*);



// Subroutines
ot_u8   sub_get_headerlen(ot_u8 tnf);
void    sub_insert_header(alp_tmpl* alp, ot_u8* hdr_position, ot_u8 hdr_len);




ot_u8 sub_get_headerlen(ot_u8 tnf) {
#if (OT_FEATURE(NDEF) == ENABLED)
    ot_u8 hdr_len;
    hdr_len     = 4;                        // Pure ALP
    hdr_len    += ((tnf == 5) << 1);        // Add Type len & ID len Fields
    hdr_len    -= (tnf == 6);               // Add Type len, Remove ID len & 2 byte ID
    return hdr_len;
#else
    return 4;
#endif
}



void sub_insert_header(alp_tmpl* alp, ot_u8* hdr_position, ot_u8 hdr_len) {
/// <LI> Add hdr_len to the queue length (cursors are already in place). </LI>
/// <LI> If using NDEF (hdr_len != 4), output header processing is ugly. </LI>
/// <LI> Pure ALP (hdr_len == 4) output header processing is universal. </LI>
/// <LI> Finally, always clear MB because now the first record is done. </LI>
    if (hdr_position == NULL) {
        hdr_position            = alp->outq->putcursor;
        alp->outq->putcursor   += hdr_len;
    }
 //#alp->outq->length  += hdr_len;
    
#   if (OT_FEATURE(NDEF) == ENABLED)
    if (hdr_len != 4) {
        *hdr_position++ = alp->outrec.flags;        //Flags byte (always)
        *hdr_position++ = 0;                        //Type Len (NDEF only)
        *hdr_position++ = alp->outrec.plength;      //Payload len
        if (alp->outrec.flags & ALP_FLAG_MB) {
            alp->outrec.flags  &= ~(NDEF_IL | 7);
            alp->outrec.flags  |= TNF_Unchanged;    // Make next record "Unchanged"
            *hdr_position++     = 2;                //NDEF ID Len
            *hdr_position++     = alp->outrec.id;   //ALP-ID    (ID 1)
            *hdr_position       = alp->outrec.cmd;  //ALP-CMD   (ID 2)
        }
    }
    else
#   else
        //*hdr_position++ = alp->outrec.flags;
        //*hdr_position++ = alp->outrec.plength;
        //*hdr_position++ = alp->outrec.id;
        //*hdr_position   = alp->outrec.cmd;
        platform_memcpy(hdr_position, &alp->outrec.flags, 4);
#   endif

    alp->outrec.flags  &= ~ALP_FLAG_MB;     
}







/** Main Library Functions <BR>
  * ========================================================================<BR>
  * These are the functions that OpenTag and your OpenTag-based code should be
  * calling.
  */

#ifndef EXTF_alp_init
void alp_init(alp_tmpl* alp, ot_queue* inq, ot_queue* outq) {
	alp->inrec.flags    = 0;
	alp->outrec.flags   = (ALP_FLAG_MB | ALP_FLAG_ME | ALP_FLAG_SR);
	alp->inq            = inq;
	alp->outq           = outq;
}
#endif


#ifndef EXTF_alp_is_available
ot_bool alp_is_available(alp_tmpl* alp, ot_int length) {
    ot_bool is_available;
    
    // Check if input queue is presently owned by some other app
    if (alp->inq->options.ubyte[0] != 0) {
        return False;
    }
    
    // Check if there is sufficient space available.  If not, try purging the
    // input and check again.
    is_available = (q_space(alp->inq) >= length);
    if (is_available == False) {
        alp_purge(alp);
        is_available = (q_space(alp->inq) >= length);
    }
    return is_available;
}
#endif



#ifndef EXTF_alp_load
ot_bool alp_load(alp_tmpl* alp, ot_u8* src, ot_int length) {
    if (alp_is_available(alp, length)) {
        alp->inq->options.ubyte[0] = 1;
        q_writestring(alp->inq, src, length);
        alp->inq->options.ubyte[0] = 0;
        return True;
    }
    return False;
}
#endif



#ifndef EXTF_alp_notify
void alp_notify(alp_tmpl* alp, ot_sig callback) {
///@todo To be completed when transformation of ALP is complete
}
#endif



#include <stdio.h>

#ifndef EXTF_alp_parse_message
ALP_status alp_parse_message(alp_tmpl* alp, id_tmpl* user_id) {
#if (OT_FEATURE(ALP) != ENABLED)
    return MSG_Null;
    
#else
    ALP_status exit_code = MSG_Null;
    
    ///@todo engage ALP locks
    
    /// Loop through records in the input message.  Each input message can
    /// generate 0 or 1 output messages.
    do {
        ot_u8*  input_position;
        ot_u8*  hdr_position;
        ot_bool atomic;
        ot_u8   hdr_len;
        ot_int  bytes;
        
        /// Safety check: make sure both queues have room remaining for the
        /// most minimal type of message, an empty message
        if (((alp->inq->back - alp->inq->getcursor) < 4) || \
            ((alp->outq->back - alp->outq->putcursor) < 4)) {
            break;
        }

        /// Load a new input record only when the last output record has the
        /// "Message End" flag set.  Therefore, it was the last record of a
        /// previous message.  If new input record header does not match
        /// OpenTag requirement, bypass it and go to the next.  Else, copy
        /// the input record to the output record.  alp_proc() will adjust
        /// the output payload length and flags, as necessary.
        if (alp->outrec.flags & ALP_FLAG_ME) {
            input_position = alp->inq->getcursor;
            if (alp_parse_header(alp) == False) {
                break;
            }
            //platform_memcpy(&alp->outrec.flags, &alp->inrec.flags, 4);
            *((ot_u32*)&alp->outrec.flags) = *((ot_u32*)&alp->inrec.flags);
        }
        
        ///@todo transform output creation part to a separate function call the
        ///      application should use when building response messages.  That
        ///      function should handle chunking.
        
        /// Reserve space in alp->outq for header data.  It is updated later.
        /// The flags and payload length are determined by processing, so this
        /// method is necessary.
        //OBSOLETE: hdr_len = sub_get_headerlen(alp->outrec.flags & 7);
        hdr_position            = alp->outq->putcursor;
        alp->outq->putcursor   += 4; //OBSOLETE: hdr_len;
        
        /// ALP Proc must write appropriate data to alp->outrec, and it must
        /// make sure not to overrun the output queue.  It must write:
        /// <LI> NDEF_CF if the output record is chunking </LI>
        /// <LI> NDEF_ME if the output record is the last in the message </LI>
        /// <LI> The output record payload length </LI>
        atomic = alp_proc(alp, user_id);
        if (alp->outrec.plength == 0) {
            // Remove header and any output data if no data written
            // Also, remove output chunking flag
            alp->outq->putcursor   = hdr_position;
            alp->outrec.flags     &= ~NDEF_CF;
        }
        else {
            //OBSOLETE: sub_insert_header(alp, hdr_position, hdr_len);
            platform_memcpy(hdr_position, &alp->outrec.flags, 4);
            alp->outrec.flags  &= ~ALP_FLAG_MB;
        }
            
        /// This version of ALP does not support nested messages.  It will
        /// terminate processing and return when the input message is ended.
        /// The if-else serves to auto-purge the last message, if possible
        if (alp->inrec.flags & ALP_FLAG_ME) {
            ot_u8* nextrecord;
            nextrecord  = input_position + input_position[1] + 4;
            if (atomic && (nextrecord == alp->inq->putcursor)) {
                alp->inq->putcursor = input_position;
                alp->inq->getcursor = input_position;
            }
            else {
                input_position[0]   = 0;
                alp->inq->getcursor = nextrecord;
            }
            
            exit_code = MSG_End;
            break;
        }
    }
    while (exit_code != MSG_Null);
    
    ///@todo release ALP locks
    
    return exit_code;
#endif
}
#endif



#ifndef EXTF_alp_new_appq
void alp_new_appq(alp_tmpl* alp, ot_queue* appq, ot_u8* front) {
    //DEBUG_ASSERT
    appq->alloc     = alp->inq->alloc;
    appq->front     = front;
    appq->back      = front + front[1] + 4;
    appq->getcursor = front;
    appq->putcursor = alp->inq->putcursor;
}
#endif


#ifndef EXTF_alp_goto_next
ot_u8 alp_goto_next(alp_tmpl* alp, ot_queue* appq, ot_u8 target) {
    //DEBUG_ASSERT
    while ((alp->inq->putcursor - appq->back) > 0) {
        appq->getcursor = appq->back;
        appq->back      = appq->getcursor + appq->getcursor[1] + 4;
        if ((appq->getcursor[0] != 0) && (appq->getcursor[2] == target)) {
            return appq->getcursor[0];
        }
    }
    return 0;
}
#endif



#ifndef EXTF_alp_retrieve_cmd
ot_u8 alp_retrieve_cmd(alp_tmpl* alp) {
    ot_u8 cmd_value;
    alp->inq->getcursor[0]  = 0;                        // Mark as read
    cmd_value               = alp->inq->getcursor[3];   // Get command value
    alp->inq->getcursor    += 4;                        // Go past Header
    
    return cmd_value;
}
#endif




#ifndef EXTF_alp_purge
void alp_purge(alp_tmpl* alp) {
    ot_u8* acursor;
    ot_u8* bcursor;
    ot_u8* ccursor;
    ot_int total_purge_bytes;

#   if (OT_FEATURE(NDEF))
#       warnining "NDEF not yet supported for non-atomic alps"
#   endif
    
    /// 1. An ALP processor that has non-atomic handling ability must mark all
    ///    record flags to 0, after that record is processed.  In the special
    ///    case where all records are marked to 0, just empty the queue.  In
    ///    the special case where no records are markeds to 0, bail out.
    {   ot_u8 other_recs    = 0;
        ot_u8 marked_recs   = 0;
        ot_u8* cursor;
        
        for (cursor=alp->inq->front; cursor<alp->inq->putcursor; cursor+=(4+cursor[1])) {
            other_recs |= cursor[0];
            if ((cursor[0] | marked_recs) == 0) {   // first marked record
                marked_recs = 1;
                acursor     = cursor;
            }
        }
        if (other_recs == 0) {
            q_empty(alp->inq);
            return;
        }
        if (marked_recs == 0) {
            return;
        }
    }
    
    /// 2. Progress through Queue, purging targeted ALPs by shifting later 
    /// contents over them.  "acursor" has been set, in part 1, to the first
    /// instance of a targeted ALP.
    total_purge_bytes = 0;
    
    while (1) {
        ot_int move_bytes;
        ot_int purge_bytes;
    
        // Go past consecutive instances of records marked for purging.
        bcursor = acursor;
        while ((bcursor[0] == 0) && (bcursor < alp->inq->putcursor))  {
            bcursor += (4 + bcursor[1]);
        }

        // Go past consecutive instances of unmarked records.
        ccursor = bcursor;
        while ((ccursor[0] != 0) && (ccursor < alp->inq->putcursor)) {
            ccursor += (4 + ccursor[1]);
        }
        
        // getcursor management: 
        // - if getcursor is less than acursor, leave alone
        // - else if getcursor is between acursor and bcursor, set to acursor
        // - else (getcursor > bcursor) subtract purge_bytes.
        purge_bytes         = (bcursor-acursor);
        total_purge_bytes  += purge_bytes;
        if (alp->inq->getcursor > acursor) {
            alp->inq->getcursor -= purge_bytes;
            if (alp->inq->getcursor < acursor)
                alp->inq->getcursor = acursor;
        }
        
        // Exit the loop if there is no more data to move.  Else, shift the 
        // non-targeted ALPs back over the targeted ones.
        move_bytes = (ccursor-bcursor);
        if (move_bytes == 0) {
            break;
        }
        platform_memcpy(acursor, bcursor, (ccursor-bcursor));   
    }
    
    ///3. Finally, retract the putcursor and length by the total number of 
    ///   purged bytes
    alp->inq->putcursor -= total_purge_bytes;
    //#alpq->length    -= total_purge_bytes;
}
#endif



#ifndef EXTF_alp_kill
void alp_kill(alp_tmpl* alp, ot_u8 kill_id) {
    ot_u8*  cursor;
    
    for (cursor=alp->inq->front; cursor<alp->inq->putcursor; cursor+=(4+cursor[1])) {
        if (cursor[2] == kill_id) {
            cursor[0]   = 0;
        }
    }
    alp_purge(alp);
}
#endif









/** Internal Module Routines <BR>
  * ========================================================================<BR>
  * Under normal software design models, these functions likely would not be 
  * exposed.  However, we expose pretty much everything in OpenTag.
  *
  * Use with caution.
  */

#ifndef EXTF_alp_get_handle
ot_u8 alp_get_handle(ot_u8 alp_id) {
#   if (ALP_API)
    if (alp_id >= 0x80) {
        alp_id -= (0x80-(ALP_FUNCTIONS-ALP_ASAPI-ALP_API));
    }
#   endif
    if (alp_id > (ALP_FUNCTIONS+1)) {
        alp_id = (ALP_FUNCTIONS+1);
    }
}
#endif


#ifndef EXTF_alp_proc
ot_bool alp_proc(alp_tmpl* alp, id_tmpl* user_id) {
    static const sub_proc proc[] = {
        &alp_proc_null,
#   if (ALP_FILESYSTEM)
        &alp_proc_filedata,
#   endif
#   if (ALP_SENSORS)
        &alp_proc_null,         //Not implemented yet
#   endif
#   if (ALP_SECURITY)
        &alp_proc_null,         //Not implemented yet
#   endif
#   if (ALP_LOGGER)
        &alp_proc_logger,
#   endif
#   if (ALP_DASHFORTH)
        &alp_proc_null,         //Not implemented yet
#   endif
#   if (ALP_API)
        &alp_proc_api_session,
        &alp_proc_api_system,
        &alp_proc_api_query,
#   endif
#   if (ALP_EXT)
        &otapi_alpext_proc,
#   else
        &alp_proc_null
#   endif
    };

    ot_u8 alp_handle;
    
    // Always flush payload length of output before any data is written
    alp->outrec.plength = 0;
    
    /// The proc function must set alp->outrec.plength based on how much
    /// data it writes to the output queue.  It must return False if the output
    /// should be canceled.
    alp_handle  = alp_get_handle(alp->inrec.id);
    alp_handle  = (ot_u8)proc[alp_handle](alp, user_id);
    
    /// If the output bookmark is non-Null, there is output chunking.  Else, 
    /// the output message is complete (ended)
    ///@todo Output bookmark is not implemented yet in any ALP.
    alp->outrec.flags   &= ~ALP_FLAG_ME;
    alp->outrec.flags   |= (alp->outrec.bookmark) ? ALP_FLAG_CF : ALP_FLAG_ME;

    // Return 0 length (False) or non-zero length (True)
    return (ot_bool)alp_handle;
}
#endif










/** Functions Under Review <BR>
  * ========================================================================<BR>
  * These are legacy functions.  They might get bundled into different 
  * functions, changed, or removed.  
  */


#ifndef EXTF_alp_break
/// @note This function is used in a presently-disabled block of code for
/// parsing multiframe M2DP streams.  That whole piece of functionality --
/// multiframe M2DP -- is getting rearchitected.  alp_break() is likely to be
/// removed in future versions of OpenTag

void alp_break(alp_tmpl* alp) {
/// Break a running stream, and manually put a break record onto the stream
#if (OT_FEATURE(NDEF) == ENABLED)
    ot_u8 tnf;
    tnf = (alp->outrec.flags & 7);
#else
#   define tnf 0
#endif
    alp->outrec.flags   = (ALP_FLAG_MB | ALP_FLAG_ME | NDEF_SR | NDEF_IL) | tnf;
    alp->outrec.plength = 0;
    alp->outrec.id      = 0;
    alp->outrec.cmd     = 0;
    
    sub_insert_header(alp, NULL, sub_get_headerlen(tnf));
}
#endif




#ifndef EXTF_alp_new_record
/// @note This function is used by the logger (OTAPI_logger.c), but nowhere 
/// else.  The ability to create a new output record/message is required, but 
/// the method of doing it may likely get re-architected.

void alp_new_record(alp_tmpl* alp, ot_u8 flags, ot_u8 payload_limit, ot_int payload_remaining) {
    // Clear control flags (begin, end, chunk)
	// Chunk and End will be intelligently set in this function, but Begin must
	// be set by the caller, AFTER this function.
	alp->outrec.flags |= flags;
	alp->outrec.flags |= NDEF_SR;
#   if (OT_FEATURE(NDEF))
	alp->outrec.flags &= ~(ALP_FLAG_ME | ALP_FLAG_CF | NDEF_IL);
#   else
    alp->outrec.flags &= (ALP_FLAG_MB | NDEF_SR);
#   endif

    // NDEF TNF needs to be 5 (with ID) on MB=1 and 6 (no ID) or MB=0
	// Pure ALP should always have IL=0 and TNF=0
#   if (OT_FEATURE(NDEF))
    if (alp->outrec.flags & 7) {
        alp->outrec.flags &= ~7;
        alp->outrec.flags |= (alp->outrec.flags & ALP_FLAG_MB) ? (NDEF_IL+5) : 6;
    }
#   endif

	// Automatically set Chunk or End.
	// "payload_remaining" is re-purposed to contain the number of bytes loaded
	// Chunk Flag is ignored by pure-ALP
	if (payload_remaining > payload_limit) {
		payload_remaining   = payload_limit;
#       if (OT_FEATURE(NDEF))
		alp->outrec.flags  |= ALP_FLAG_CF;
#       endif
	}
	else {
		alp->outrec.flags  |= ALP_FLAG_ME;
	}

	alp->outrec.plength = (ot_u8)payload_remaining;
	sub_insert_header(alp, NULL, sub_get_headerlen(alp->outrec.flags&7));
}
#endif



/* PENDING REMOVAL

#ifndef EXTF_alp_new_message
void alp_new_message(alp_tmpl* alp, ot_u8 payload_limit, ot_int payload_remaining) {
/// Prepare the flags and payload length for the first record in a message
	alp_new_record(alp, ALP_FLAG_MB, payload_limit, payload_remaining);
}
#endif
*/


/* alp_parse_header PENDING REMOVAL */

#ifndef EXTF_alp_parse_header
ot_bool alp_parse_header(alp_tmpl* alp) {
    ot_u8* msgfront;
    msgfront                = alp->inq->getcursor;
    alp->inq->getcursor    += 4;
    platform_memcpy(&alp->inrec.flags, msgfront, 4);
    
    ///@todo this is legacy code.  Bookmark should get removed in future
    if (alp->inrec.flags & ALP_FLAG_MB) {
        alp->inrec.bookmark     = NULL;
    	alp->outrec.bookmark    = NULL;
    }

    ///@todo could do some checking here, not sure if necessary though.
    return True;

    ///@note Old, Obsolete code is below
    
    // ALP & NDEF Universal Field (Flags)
//    alp->inrec.flags = *alp->inq->getcursor++;

    // OBSOLETE: Clear bookmarks on new message input 
//    if (alp->inrec.flags & ALP_FLAG_MB) {
//    	alp->inrec.bookmark     = NULL;
//    	alp->outrec.bookmark    = NULL;
//    }

    // ALP type
//    if ((alp->inrec.flags & (NDEF_SR+NDEF_IL+7)) == (NDEF_SR+0)) {
//    	ot_u8* qdata;
//    	qdata                   = alp->inq->getcursor;
//    	alp->inq->getcursor    += 3;
//    	platform_memcpy(&alp->inrec.plength, qdata, 3);
//    	return True;
//    }

    

// Obsolete
//#if (OT_FEATURE(NDEF) == ENABLED)
//    // NDEF Universal Fields
//    alp->inq->getcursor++;	                            //bypass type length
//    alp->inrec.plength  = *alp->inq->getcursor++;       //get payload length
//    
//    // NDEF Type Unchanged (for Chunking)
//    if ((alp->inrec.flags & (NDEF_MB+NDEF_SR+NDEF_IL+7)) == (NDEF_SR+6)) {
//        return True;
//    }
//    
//    // NDEF Type Unknown (for MB==1)
//    if ((alp->inrec.flags & (NDEF_MB+NDEF_SR+NDEF_IL+7)) == (NDEF_MB+NDEF_SR+NDEF_IL+5)) {
//    	if (*alp->inq->getcursor++ == 2) {
//    		alp->inrec.id   = *alp->inq->getcursor++;
//            alp->inrec.cmd  = *alp->inq->getcursor++;
//            return True;
//    	}
//    }
//#endif
//
//    return False;
}
#endif




#ifndef EXTF_alp_load_retval
///@note alp_load_retval() is something of a special-purpose routine for a 
/// legacy API.  Very likely it will be refactored.

ot_bool alp_load_retval(alp_tmpl* alp, ot_u16 retval) {
/// This function is for writing a two-byte integer to the return record.  It
/// is useful for some types of API return sequences
    ot_bool respond = (ot_bool)(alp->outrec.cmd & 0x80);

    if (respond)  {
      //alp->outrec.flags          &= ~ALP_FLAG_CF;
        alp->outrec.plength = 2;
        alp->outrec.cmd    |= 0x40;
        q_writeshort(alp->outq, retval);
    }
    
    return respond;
}
#endif




///@note this function is a recent inclusion, but it's not used.  Don't use it
/// until new ALP is stable, if it is still here.
void alp_load_header(ot_queue* appq, alp_record* rec) {
    rec->flags      = q_readbyte(appq);
    rec->plength    = q_readbyte(appq);
    rec->id         = q_readbyte(appq);
    rec->cmd        = q_readbyte(appq);
    //q_readstring(appq, (ot_u8*)&rec->flags, 4);  //works only on packed structs
}









/** Protocol Processors <BR>
  * ========================================================================<BR>
  * The Null Processor is implemented here.  The rest of the processors are
  * implemented in separate C files, named alp_...c
  */

#ifndef EXTF_alp_proc_null
ot_bool alp_proc_null(alp_tmpl* a0, id_tmpl* a1) {
	return True;   // Atomic, with no payload data
}
#endif




#endif

