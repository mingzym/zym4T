/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/***************************************************************************
 LogObject.cc

 
 ***************************************************************************/
#include "inktomi++.h"


#include "Error.h"
#include "P_EventSystem.h"
#include "LogUtils.h"
#include "LogField.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogAccess.h"
#include "Log.h"
#include "LogObject.h"

LogBufferManager::~LogBufferManager()
{
  for (int i = 0; i < DELAY_DELETE_SIZE; i++) {
    delete _delay_delete_array[i];
    _delay_delete_array[i] = 0;
  }
  ink_mutex_acquire(&_flush_array_mutex);
  ink_mutex_release(&_flush_array_mutex);
  ink_mutex_destroy(&_flush_array_mutex);
}

size_t
LogBufferManager::flush_buffers(LogBufferSink * sink, size_t * to_disk, size_t * to_net, size_t * to_pipe)
{
  LogBuffer *flush_buffer, *wlist;
  size_t total_bytes_flushed = 0;

  if ((wlist = get_flush_queue()) != 0) {
    int delcnt = 0;

    while ((flush_buffer = wlist) != 0) {
      wlist = flush_buffer->next_flush;
      flush_buffer->update_header_data();
      int bytes_flushed = sink->write(flush_buffer, to_disk, to_net, to_pipe);
      if (bytes_flushed > 0)
        total_bytes_flushed += bytes_flushed;
      delete _delay_delete_array[_head];
      _delay_delete_array[_head] = flush_buffer;
      _head = ++_head % DELAY_DELETE_SIZE;
      delcnt++;
    }
    Debug("log2-logbuffer", "flushed %d buffers: %lu bytes", delcnt, (unsigned long) total_bytes_flushed);
  }
  return total_bytes_flushed;
}

/*-------------------------------------------------------------------------
  LogObject
  -------------------------------------------------------------------------*/

LogObject::LogObject(LogFormat * format, const char *log_dir,
                     const char *basename, LogFileFormat file_format,
                     const char *header, int rolling_enabled,
                     int rolling_interval_sec, int rolling_offset_hr, int rolling_size_mb)
:m_alt_filename(NULL)
  , m_flags(0)
  , m_signature(0)
  , m_ref_count(0)
  , m_log_buffer(NULL)
{
  LogBuffer *tmp_lb_array[(DELAY_DELETE_SIZE + (DELAY_DELETE_SIZE / 4))];
  int i;

  ink_debug_assert(format != NULL);
  m_format = new LogFormat(*format);

  if (file_format == BINARY_LOG) {
    m_flags |= BINARY;
  } else if (file_format == ASCII_PIPE) {
#ifdef ASCII_PIPE_FORMAT_SUPPORTED
    m_flags |= WRITES_TO_PIPE;
#else
    // ASCII_PIPE not supported, reset to ASCII_LOG
    Warning("ASCII_PIPE Mode not supported, resetting Mode to ASCII_LOG " "for LogObject %s", basename);
    file_format = ASCII_LOG;
#endif
  };

  generate_filenames(log_dir, basename, file_format);

  // compute_signature is a static function
  m_signature = compute_signature(m_format, m_basename, m_flags);

#ifndef TS_MICRO
  // by default, create a LogFile for this object, if a loghost is 
  // later specified, then we will delete the LogFile object
  //
  m_logFile = NEW(new LogFile(m_filename, header, file_format,
                              m_signature,
                              Log::config->ascii_buffer_size,
                              Log::config->max_line_size, Log::config->overspill_report_count));
#endif // TS_MICRO

  m_log_buffer = NEW(new LogBuffer(this, Log::config->log_buffer_size));
  ink_assert(m_log_buffer != NULL);

  // preallocate LogBuffers in order to use cont memory
  for (i = 0; i < (DELAY_DELETE_SIZE + (DELAY_DELETE_SIZE / 4)); i++) {
    tmp_lb_array[i] = NEW(new LogBuffer(this, Log::config->log_buffer_size));
    ink_assert(tmp_lb_array[i] != NULL);
  }
  for (i = 0; i < (DELAY_DELETE_SIZE + (DELAY_DELETE_SIZE / 4)); i++) {
    delete tmp_lb_array[i];
  }

  _setup_rolling(rolling_enabled, rolling_interval_sec, rolling_offset_hr, rolling_size_mb);
  m_last_roll_time = LogUtils::timestamp();

  Debug("log2-config", "exiting LogObject constructor, filename=%s this=%p", m_filename, this);
}

LogObject::~LogObject()
{
  Debug("log2-config", "entering LogObject destructor, this=%p", this);

  while (m_ref_count > 0) {
    Debug("log2-config", "LogObject refcount = %d, waiting for zero", m_ref_count);
  }

  flush_buffers(0, 0, 0);

  delete m_logFile;
  xfree(m_basename);
  xfree(m_filename);
  xfree(m_alt_filename);
  delete m_format;
  delete m_log_buffer;
}

//-----------------------------------------------------------------------------
//
// This function generates an object filename according to the following rules:
//
// 1.- if no extension is given, add .log for ascii logs, and .blog for
//     binary logs
// 2.- if an extension is given, then do not modify filename and use that
//     extension regardless of type of log
// 3.- if there is a '.' at the end of the name, then do not add an extension
//     and remove the '.'. To have a dot at the end of the filename, specify
//     two ('..').
//
void
LogObject::generate_filenames(const char *log_dir, const char *basename, LogFileFormat file_format)
{
  ink_debug_assert(log_dir && basename);

  int i = -1, len = 0;
  char c;
  while (c = basename[len], c != 0) {
    if (c == '.') {
      i = len;
    }
    ++len;
  }
  if (i == len - 1) {
    --len;
  };                            // remove dot at end of name

  char *ext = 0;
  int ext_len = 0;
  if (i < 0) {                  // no extension, add one
    switch (file_format) {
    case ASCII_LOG:
      ext = ASCII_LOG_OBJECT_FILENAME_EXTENSION;
      ext_len = 4;
      break;
    case BINARY_LOG:
      ext = BINARY_LOG_OBJECT_FILENAME_EXTENSION;
      ext_len = 5;
      break;
    case ASCII_PIPE:
      ext = ASCII_PIPE_OBJECT_FILENAME_EXTENSION;
      ext_len = 5;
      break;
    default:
      ink_debug_assert(!"unknown file format");
    }
  }

  int dir_len = (int) strlen(log_dir);
  int basename_len = len + ext_len + 1; // include null terminator
  int total_len = dir_len + 1 + basename_len;   // include '/'

  m_filename = (char *) xmalloc(total_len);
  m_basename = (char *) xmalloc(basename_len);

  memcpy(m_filename, log_dir, dir_len);
  m_filename[dir_len++] = '/';
  memcpy(&m_filename[dir_len], basename, len);
  memcpy(m_basename, basename, len);

  if (ext_len) {
    memcpy(&m_filename[dir_len + len], ext, ext_len);
    memcpy(&m_basename[len], ext, ext_len);
  }
  m_filename[total_len - 1] = 0;
  m_basename[basename_len - 1] = 0;
}

void
LogObject::rename(char *new_name)
{
  // NOTE: this function is intended to be called by the LogObjectManager
  // while solving filename conflicts. It DOES NOT modify the signature of
  // the LogObject to match the new filename.
  //
  xfree(m_alt_filename);
  m_alt_filename = xstrdup(new_name);
  m_logFile->change_name(new_name);
}

void
LogObject::add_filter(LogFilter * filter, bool copy)
{
  if (!filter) {
    return;
  }
  m_filter_list.add(filter, copy);
}

void
LogObject::set_filter_list(const LogFilterList & list, bool copy)
{
  m_filter_list.clear();
  LogFilter *f;
  for (f = list.first(); f != NULL; f = list.next(f)) {
    m_filter_list.add(f, copy);
  }
  m_filter_list.set_conjunction(list.does_conjunction());
}

void
LogObject::add_loghost(LogHost * host, bool copy)
{
  if (!host) {
    return;
  }
  m_host_list.add(host, copy);

  // A LogObject either writes to a file, or sends to a collation host, but
  // not both. By default, it writes to a file. If a LogHost is specified,
  // then delete the LogFile object
  //
  if (m_logFile) {
    delete m_logFile;
    m_logFile = NULL;
  };
}

// we conpute the object signature from the fieldlist_str and the printf_str
// of the LogFormat rather than from the format_str because the format_str
// is not part of a LogBuffer header 
//
inku64 LogObject::compute_signature(LogFormat * format, char *filename, unsigned int flags)
{
  char *
    fl = format->fieldlist();
  char *
    ps = format->printf_str();
  inku64
    signature = 0;

  if (fl && ps && filename) {
    int
      buf_size = strlen(fl) + strlen(ps) + strlen(filename) + 2;

    char *
      buffer = (char *) xmalloc(buf_size);

    ink_string_concatenate_strings(buffer,
                                   fl,
                                   ps,
                                   filename,
                                   flags & LogObject::BINARY ? "B" :
                                   (flags & LogObject::WRITES_TO_PIPE ? "P" : "A"), NULL);

    INK_MD5
      md5s;
    md5s.encodeBuffer(buffer, buf_size - 1);
    signature = md5s.fold();

    xfree(buffer);
  }
  return signature;
}

void
LogObject::display(FILE * fd)
{
  fprintf(fd, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  fprintf(fd, "LogObject [%p]: format = %s (%p)\nbasename = %s\n" "flags = %u\n"
#ifdef __alpha
          // the DEC does not like %llu
          "signature = %lu\n",
#else
          "signature = %llu\n",
#endif
          this, m_format->name(), m_format, m_basename, m_flags, m_signature);
#ifndef TS_MICRO
  if (is_collation_client()) {
    m_host_list.display(fd);
  } else {
#endif // TS_MICRO
    fprintf(fd, "full path = %s\n", get_full_filename());
#ifndef TS_MICRO
  }
#endif // TS_MICRO
  m_filter_list.display(fd);
  fprintf(fd, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
}

#ifndef TS_MICRO
void
LogObject::displayAsXML(FILE * fd, bool extended)
{
  if (extended) {
    // display format and filter objects
    m_format->displayAsXML(fd);
    m_filter_list.display_as_XML(fd);
  }

  fprintf(fd,
          "<LogObject>\n"
          "  <Mode        = \"%s\"/>\n"
          "  <Format      = \"%s\"/>\n"
          "  <Filename    = \"%s\"/>\n", (m_flags & BINARY ? "binary" : "ascii"), m_format->name(), m_filename);

  LogFilter *filter;
  for (filter = m_filter_list.first(); filter != NULL; filter = m_filter_list.next(filter)) {
    fprintf(fd, "  <Filter      = \"%s\"/>\n", filter->name());
  }

  LogHost *host;
  for (host = m_host_list.first(); host != NULL; host = m_host_list.next(host)) {
    fprintf(fd, "  <LogHostName = \"%s\"/>\n", host->name());
  }

  fprintf(fd, "</LogObject>\n");
}
#endif // TS_MICRO

LogBuffer *
LogObject::_checkout_write(size_t * write_offset, size_t bytes_needed)
{
  LogBuffer::LB_ResultCode result_code;

  LogBuffer *buffer;
  LogBuffer *new_buffer;

  bool retry = true;

  do {
    buffer = m_log_buffer;
    result_code = buffer->checkout_write(write_offset, bytes_needed);

    switch (result_code) {
    case LogBuffer::LB_OK:
      // checkout succeded
      //
      retry = false;
      break;

    case LogBuffer::LB_FULL_ACTIVE_WRITERS:
    case LogBuffer::LB_FULL_NO_WRITERS:

      // no more room in current buffer, create a new one
      //
      new_buffer = NEW(new LogBuffer(this, Log::config->log_buffer_size));

      // swap the new buffer for the old one (only this thread 
      // should be doing this, so there should be no problem)
      //
      INK_WRITE_MEMORY_BARRIER;
      ink_atomic_swap_ptr((void *) &m_log_buffer, new_buffer);

      if (result_code == LogBuffer::LB_FULL_NO_WRITERS) {
        // there are no writers, move the buffer to the flush list
        //
        Debug("log2-logbuffer", "adding buffer %d to flush list after checkout", buffer->get_id());

        m_buffer_manager.add_to_flush_queue(buffer);
        ink_mutex_acquire(&Log::flush_mutex);
        Log::flush_counter++;
        ink_cond_signal(&Log::flush_cond);
        ink_mutex_release(&Log::flush_mutex);
      }
      // fallover to retry

    case LogBuffer::LB_RETRY:
      // no more room, but another thread should be taking care of
      // creating a new buffer, so try again
      //
      break;

    case LogBuffer::LB_BUFFER_TOO_SMALL:

      // return a null buffer to signal the caller that this
      // transaction cannot be logged
      //
      retry = false;
      buffer = NULL;
      break;

    default:
      ink_debug_assert(false);
    }

  } while (retry && write_offset);      // if write_offset is null, we do
  // not retry because we really do
  // not want to write to the buffer
  // only to set it as full

  return buffer;
}

int
LogObject::log(LogAccess * lad, char *text_entry)
{
  LogBuffer *buffer;

  // mutex used for the statistics (used in LOG_INCREMENT_DYN_STAT macro)
  ProxyMutex *mutex = this_ethread()->mutex;

  size_t offset = 0;            // prevent warning
  size_t bytes_needed = 0, bytes_used = 0;

  // log to a pipe even if space is exhausted since pipe uses no space
  // likewise, send data to a remote client even if local space is exhausted
  // (if there is a remote client, m_logFile will be NULL
  if (Log::config->logging_space_exhausted && !writes_to_pipe() && m_logFile) {
    LOG_INCREMENT_DYN_STAT(log2_stat_event_log_access_fail_stat);
    return Log::FULL;
  }
  // this verification must be done here in order to avoid 'dead' LogBuffers
  // with none zero 'in usage' counters (see _checkout_write for more details)
  if (!lad && !text_entry) {
    Note("Call to LogAccess without LAD or text entry; skipping");
    LOG_INCREMENT_DYN_STAT(log2_stat_event_log_access_fail_stat);
    return Log::FAIL;
  }

  RefCounter counter(&m_ref_count);     // scope exit will decrement

  if (lad && m_filter_list.toss_this_entry(lad)) {
    Debug("log2", "entry filtered, skipping ...");
    LOG_INCREMENT_DYN_STAT(log2_stat_event_log_access_skip_stat);
    return Log::SKIP;
  }

  if (lad && m_format->is_aggregate()) {
    // marshal the field data into the temp space provided by the
    // LogFormat object for aggregate formats
    if (m_format->m_agg_marshal_space == NULL) {
      Note("No temp space to marshal aggregate fields into");
      LOG_INCREMENT_DYN_STAT(log2_stat_event_log_access_fail_stat);
      return Log::FAIL;
    }

    long time_now = LogUtils::timestamp();
    m_format->m_field_list.marshal(lad, m_format->m_agg_marshal_space);

    // step through each of the fields and update the LogField object
    // with the newly-marshalled data
    LogFieldList *fl = &m_format->m_field_list;
    char *data_ptr = m_format->m_agg_marshal_space;
    LogField *f;
    LOG_INT val;
    for (f = fl->first(); f; f = fl->next(f)) {
      // convert to host order to do computations
      val = (f->is_time_field())? time_now : ntohl(*((LOG_INT *) data_ptr));
      f->update_aggregate(val);
      data_ptr += MIN_ALIGN;
    }

    if (time_now < m_format->m_interval_next) {
      Debug("log2-agg", "Time now = %d, next agg = %d; not time "
            "for aggregate entry", time_now, m_format->m_interval_next);
      return Log::LOG_OK;
    }
    // can easily compute bytes_needed because all fields are INTs
    // and will use MIN_ALIGN each
    bytes_needed = m_format->field_count() * MIN_ALIGN;
  } else if (lad) {
    bytes_needed = m_format->m_field_list.marshal_len(lad);
  } else if (text_entry) {
    bytes_needed = LogAccess::strlen(text_entry);
  }

  if (bytes_needed == 0) {
    Debug("log2-buffer", "Nothing to log, bytes_needed = 0");
    LOG_INCREMENT_DYN_STAT(log2_stat_event_log_access_skip_stat);
    return Log::SKIP;
  }
  // Now try to place this entry in the current LogBuffer.

  buffer = _checkout_write(&offset, bytes_needed);

  if (!buffer) {
    Note("Traffic Server is skipping the current log entry for %s because "
         "its size (%d) exceeds the maximum payload space in a " "log buffer", m_basename, bytes_needed);
    LOG_INCREMENT_DYN_STAT(log2_stat_event_log_access_fail_stat);
    return Log::FAIL;
  }
  //
  // Ok, the checkout_write was successful, which means we have a valid
  // offset into the current buffer.  Marshal the entry into the buffer,
  // and the commit (checkin) the changes.
  //

  if (lad && m_format->is_aggregate()) {
    // the "real" entry data is contained in the LogField objects
    // themselves, not in this lad.
    bytes_used = m_format->m_field_list.marshal_agg(&(*buffer)[offset]);
    ink_assert(bytes_needed >= bytes_used);
    m_format->m_interval_next += m_format->m_interval_sec;
    Debug("log2-agg", "Aggregate entry created; next time is %d", m_format->m_interval_next);
  } else if (lad) {
    bytes_used = m_format->m_field_list.marshal(lad, &(*buffer)[offset]);
    ink_assert(bytes_needed >= bytes_used);
  } else if (text_entry) {
    ink_strncpy(&(*buffer)[offset], text_entry, bytes_needed);
  }

  LogBuffer::LB_ResultCode result_code = buffer->checkin_write(offset);

  if (result_code == LogBuffer::LB_ALL_WRITERS_DONE) {
    // all checkins completed, put this buffer in the flush list
    Debug("log2-logbuffer", "adding buffer %d to flush list after checkin", buffer->get_id());

    m_buffer_manager.add_to_flush_queue(buffer);
    ink_mutex_acquire(&Log::flush_mutex);
    Log::flush_counter++;
//      ink_cond_signal (&Log::flush_cond);
    ink_mutex_release(&Log::flush_mutex);
  }

  LOG_INCREMENT_DYN_STAT(log2_stat_event_log_access_stat);

  return Log::LOG_OK;
}

void
LogObject::_setup_rolling(int rolling_enabled, int rolling_interval_sec, int rolling_offset_hr, int rolling_size_mb)
{
  if (rolling_enabled <= LogConfig::NO_ROLLING || rolling_enabled >= LogConfig::INVALID_ROLLING_VALUE) {
    m_rolling_enabled = LogConfig::NO_ROLLING;
    m_rolling_interval_sec = 0;
    m_rolling_offset_hr = 0;
    m_rolling_size_mb = 0;
    if (rolling_enabled != LogConfig::NO_ROLLING) {
      Warning("Valid rolling_enabled values are %d to %d, invalid value "
              "(%d) specified for %s, rolling will be disabled for this file.",
              LogConfig::NO_ROLLING, LogConfig::INVALID_ROLLING_VALUE - 1, rolling_enabled, m_filename);
    } else {
      Status("Rolling disabled for %s", m_filename);
    }
  } else {
    // do checks for rolling based on time
    //
    if (rolling_enabled == LogConfig::ROLL_ON_TIME_ONLY ||
        rolling_enabled == LogConfig::ROLL_ON_TIME_OR_SIZE || rolling_enabled == LogConfig::ROLL_ON_TIME_AND_SIZE) {
      if (rolling_interval_sec < LogConfig::MIN_ROLLING_INTERVAL_SEC) {
        // check minimum 
        m_rolling_interval_sec = LogConfig::MIN_ROLLING_INTERVAL_SEC;
      } else if (rolling_interval_sec > 86400) {
        // 1 day maximum
        m_rolling_interval_sec = 86400;
      } else if (86400 % rolling_interval_sec == 0) {
        // OK, divides day evenly
        m_rolling_interval_sec = rolling_interval_sec;
      } else {
        m_rolling_interval_sec = rolling_interval_sec;
        // increase so it divides day evenly
        while (86400 % ++m_rolling_interval_sec);
      }

      if (m_rolling_interval_sec != rolling_interval_sec) {
        Note("Rolling interval adjusted from %d sec to %d sec for %s",
             rolling_interval_sec, m_rolling_interval_sec, m_filename);
      }

      if (rolling_offset_hr<0 || rolling_offset_hr> 23) {
        rolling_offset_hr = 0;
        Note("Rolling offset out of bounds for %s, setting it to %d", m_filename, rolling_offset_hr);
      }

      m_rolling_offset_hr = rolling_offset_hr;
    }

    if (rolling_enabled == LogConfig::ROLL_ON_SIZE_ONLY ||
        rolling_enabled == LogConfig::ROLL_ON_TIME_OR_SIZE || rolling_enabled == LogConfig::ROLL_ON_TIME_AND_SIZE) {
      if (rolling_size_mb <= 0) {
        rolling_size_mb = 10;
        Note("Rolling size invalid for %s, setting it to %d MB", m_filename, rolling_size_mb);
      }

      m_rolling_size_mb = rolling_size_mb;
    }

    m_rolling_enabled = rolling_enabled;
  }
}

int
LogObject::roll_files_if_needed(long time_now)
{
  if (!m_rolling_enabled)
    return 0;

  int num_rolled = 0;
  bool roll_on_time = false;
  bool roll_on_size = false;

  if (!time_now)
    time_now = LogUtils::timestamp();

  if (m_rolling_enabled != LogConfig::ROLL_ON_SIZE_ONLY) {
    if (m_rolling_interval_sec > 0) {
      // We make no assumptions about the current time not having
      // changed underneath us. This could happen during daylight
      // savings time adjustments, or if time is adjusted via NTP.
      //
      // For this reason we don't cache the number of seconds 
      // remaining until the next roll, but we calculate this figure
      // every time ...
      //
      int secs_to_next = LogUtils::seconds_to_next_roll(time_now, m_rolling_offset_hr,
                                                        m_rolling_interval_sec);

      // ... likewise, we make sure we compute the absolute value 
      // of the seconds since the last roll (which would otherwise
      // be negative if time "went back"). We will use this value
      // to make sure we don't roll twice if time goes back shortly
      // after rolling.
      // 
      int secs_since_last = (m_last_roll_time < time_now ? time_now - m_last_roll_time : m_last_roll_time - time_now);

      // number of seconds we allow for periodic_tasks() not to be
      // called and still be able to roll
      //
      const int missed_window = 10;

      roll_on_time =
        ((secs_to_next == 0 ||
          secs_to_next >= m_rolling_interval_sec - missed_window) && secs_since_last > missed_window);
    }
  }

  if (m_rolling_enabled != LogConfig::ROLL_ON_TIME_ONLY) {
    if (m_rolling_size_mb) {
      // Get file size and check if the file size if greater than the
      // configured file size for rolling
      roll_on_size = (get_file_size_bytes() > m_rolling_size_mb * LOG_MEGABYTE);
    }
  }

  if ((roll_on_time && (m_rolling_enabled == LogConfig::ROLL_ON_TIME_ONLY ||
                        m_rolling_enabled == LogConfig::ROLL_ON_TIME_OR_SIZE))
      ||
      (roll_on_size && (m_rolling_enabled == LogConfig::ROLL_ON_SIZE_ONLY ||
                        m_rolling_enabled == LogConfig::ROLL_ON_TIME_OR_SIZE))
      || (roll_on_time && roll_on_size && m_rolling_enabled == LogConfig::ROLL_ON_TIME_AND_SIZE)) {
    num_rolled = roll_files(time_now);
  }

  return num_rolled;
}

int
LogObject::_roll_files(long last_roll_time, long time_now)
{
  int num_rolled = 0;
  if (m_logFile) {
    // no need to roll if object writes to a pipe
    if (!writes_to_pipe()) {
      num_rolled += m_logFile->roll(last_roll_time, time_now);
    }
  } else {
    LogHost *host;
    for (host = m_host_list.first(); host; host = m_host_list.next(host)) {
      LogFile *orphan_logfile = host->get_orphan_logfile();
      if (orphan_logfile) {
        num_rolled += orphan_logfile->roll(last_roll_time, time_now);
      }
    }
  }
  m_last_roll_time = time_now;
  return num_rolled;
}

void
LogObject::check_buffer_expiration(long time_now)
{
  LogBuffer *b = m_log_buffer;
  if (b && time_now > b->expiration_time()) {
    force_new_buffer();
  }
}

// make sure that we will be able to write the logs to the disk
//
int
LogObject::do_filesystem_checks()
{
  if (m_logFile) {
    return m_logFile->do_filesystem_checks();
  } else {
    return m_host_list.do_filesystem_checks();
  }
}

/*-------------------------------------------------------------------------
  TextLogObject::TextLogObject
  -------------------------------------------------------------------------*/

TextLogObject::TextLogObject(const char *name, const char *log_dir, bool timestamps, const char *header, int rolling_enabled, int rolling_interval_sec, int rolling_offset_hr, int rolling_size_mb):

LogObject(NEW(new LogFormat(TEXT_LOG)), log_dir, name, ASCII_LOG, header,
          rolling_enabled, rolling_interval_sec, rolling_offset_hr, rolling_size_mb), m_timestamps(timestamps)
{
}

/*-------------------------------------------------------------------------
  TextLogObject::write

  This routine will take a printf-style format string and variable number
  of arguments, and write them to the text file.

  It really just creates a va_list and calls va_write to do the work.
  Returns the number of bytes written to the file.
  -------------------------------------------------------------------------*/

int
TextLogObject::write(const char *format, ...)
{
  int ret_val;

  ink_debug_assert(format != NULL);
  va_list ap;
  va_start(ap, format);
  ret_val = va_write(format, ap);
  va_end(ap);

  return ret_val;
}

/*-------------------------------------------------------------------------
  TextLogObject::va_write

  This routine will take a format string and va_list and write it as a
  single entry (line) in the text file.  If timestamps are on, then the
  entry will be preceeded by a timestamp.

  Returns the number of bytes written to the file.
  -------------------------------------------------------------------------*/

int
TextLogObject::va_write(const char *format, va_list ap)
{
  static const int MAX_ENTRY = 16 * LOG_KILOBYTE;
  char entry[MAX_ENTRY];
  int len;

  ink_debug_assert(format != NULL);
  len = 0;

  if (m_timestamps) {
    len = LogUtils::timestamp_to_str(LogUtils::timestamp(), entry, MAX_ENTRY);
    if (len <= 0) {
      return Log::FAIL;
    }
    //
    // Add a space after the timestamp
    //
    entry[len++] = ' ';
  }

  if (len >= MAX_ENTRY) {
    return Log::FAIL;
  }

  len += ink_vsnprintf(&entry[len], MAX_ENTRY - len, format, ap);

  //
  // Now that we have an entry and it's length (len), we can place it
  // into the associated logbuffer.
  //

  return log(NULL, entry);
}

/*-------------------------------------------------------------------------
  LogObjectManager
  -------------------------------------------------------------------------*/

void
LogObjectManager::_add_object(LogObject * object)
{
  if (_numObjects == _maxObjects) {
    _maxObjects += LOG_OBJECT_ARRAY_DELTA;
    LogObject **_new_objects = new LogObject *[_maxObjects];

    for (size_t i = 0; i < _numObjects; i++) {
      _new_objects[i] = _objects[i];
    }
    delete[]_objects;
    _objects = _new_objects;
  }

  _objects[_numObjects++] = object;
}

// _add_api_object must be called with the _APImutex held
//
void
LogObjectManager::_add_api_object(LogObject * object)
{
  if (_numAPIobjects == _maxAPIobjects) {
    _maxAPIobjects += LOG_OBJECT_ARRAY_DELTA;
    LogObject **_new_objects = new LogObject *[_maxAPIobjects];

    for (size_t i = 0; i < _numAPIobjects; i++) {
      _new_objects[i] = _APIobjects[i];
    }
    delete[]_APIobjects;
    _APIobjects = _new_objects;
  }

  _APIobjects[_numAPIobjects++] = object;
}


int
LogObjectManager::_manage_object(LogObject * log_object, bool is_api_object, int maxConflicts)
{
  if (is_api_object) {
    ACQUIRE_API_MUTEX("A LogObjectManager::_manage_object");
  }

  bool col_client = log_object->is_collation_client();
  int retVal = _solve_internal_filename_conflicts(log_object, maxConflicts);

  if (retVal == NO_FILENAME_CONFLICTS) {
    // check for external conflicts only if the object is not a collation
    // client
    //
    if (col_client || (retVal = _solve_filename_conflicts(log_object, maxConflicts), retVal == NO_FILENAME_CONFLICTS)) {

      // do filesystem checks
      //
      if (log_object->do_filesystem_checks() < 0) {
        char *msg = "The log file %s did not pass filesystem checks. " "No output will be produced for this log";
        Error(msg, log_object->get_full_filename());
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, log_object->get_full_filename());
        retVal = ERROR_DOING_FILESYSTEM_CHECKS;

      } else {

        // no conflicts, add object to the list of managed objects
        //
        if (is_api_object) {
          _add_api_object(log_object);
        } else {
          _add_object(log_object);
        }

        Debug("log2", "LogObjectManager managing object %s (%s) "
              "[signature = %llu, address = %p]",
              log_object->get_base_filename(),
              col_client ? "collation client" :
              log_object->get_full_filename(), log_object->get_signature(), log_object);

        if (log_object->has_alternate_name()) {
          Warning("The full path for the (%s) LogObject %s "
                  "with signature %llu "
                  "has been set to %s rather than %s because the latter "
                  "is being used by another LogObject",
                  log_object->receives_remote_data()? "remote" : "local",
                  log_object->get_base_filename(),
                  log_object->get_signature(), log_object->get_full_filename(), log_object->get_original_filename());
        }
      }
    }
  }

  if (is_api_object) {
    RELEASE_API_MUTEX("R LogObjectManager::_manage_object");
  }

  return retVal;
}

int
LogObjectManager::_solve_filename_conflicts(LogObject * log_object, int maxConflicts)
{
  int retVal = NO_FILENAME_CONFLICTS;

#ifndef TS_MICRO
  char *filename = log_object->get_full_filename();
  if (access(filename, F_OK)) {
    if (errno != ENOENT) {
      char *msg = "Cannot access log file %s: %s";
      char *se = strerror(errno);
      Error(msg, filename, se);
      LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename, se);
      retVal = ERROR_ACCESSING_LOG_FILE;
    }
  } else {
    // file exists, try to read metafile to get object signature
    //
    inku64 signature = 0;
    MetaInfo meta_info(filename);
    bool conflicts = true;

    if (meta_info.file_open_successful()) {
      if (meta_info.pre_panda_metafile()) {
        // assume no conflicts if pre-panda metafile and 
        // write Panda style metafile with old creation_time and
        // signature of the object requesting filename
        //
        time_t creation_time = 0;
        meta_info.get_creation_time(&creation_time);
        MetaInfo new_meta_info(filename, creation_time, log_object->get_signature());
        conflicts = false;
        Warning("Assuming no format conflicts exist for %s", filename);
        Note("Added object_signature to metafile of %s", filename);
      } else {
        bool got_sig = meta_info.get_log_object_signature(&signature);
        inku64 obj_sig = log_object->get_signature();
        if (got_sig && signature == obj_sig) {
          conflicts = false;
        }
        Debug("log2", "LogObjectManager::_solve_filename_conflicts\n"
              "\tfilename = %s\n"
              "\tmeta file signature = %llu\n"
              "\tlog object signature = %llu\n" "\tconflicts = %d", filename, signature, obj_sig, conflicts);
      }
    }

    if (conflicts) {
      if (maxConflicts == 0) {
        // do not take any action, and return an error status
        //
        char *msg = "Cannot solve filename conflicts for log file %s";
        Error(msg, filename);
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename);
        retVal = CANNOT_SOLVE_FILENAME_CONFLICTS;
      } else {
        // either the meta file could not be read, or the new object's
        // signature and the metafile signature do not match ==>
        // roll old filename so the new object can use the filename
        // it requested (previously we used to rename the NEW file
        // but now we roll the OLD file), or if the log object writes
        // to a pipe, just remove the file if it was open as a pipe

        bool roll_file = true;
#ifdef ASCII_PIPE_FORMAT_SUPPORTED
        if (log_object->writes_to_pipe()) {
          // determine if existing file is a pipe, and remove it if
          // that is the case so the right metadata for the new pipe
          // is created later
          //
          struct stat s;
          if (stat(filename, &s) < 0) {
            // an error happened while trying to get file info
            // 
            char *msg = "Cannot stat log file %s: %s";
            char *se = strerror(errno);
            Error(msg, filename, se);
            LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename, se);
            retVal = ERROR_DETERMINING_FILE_INFO;
            roll_file = false;
          } else {
            if (s.st_mode == S_IFIFO) {
              unlink(filename);
              roll_file = false;
            }
          }
        }
#endif
        if (roll_file) {
          Warning("File %s will be rolled because a LogObject with "
                  "different format is requesting the same " "filename", filename);
          LogFile logfile(filename, NULL, ASCII_LOG, 0);
          long time_now = LogUtils::timestamp();
          if (logfile.roll(time_now - log_object->get_rolling_interval(), time_now) == 0) {
            // an error happened while trying to roll the file
            // 
            char *msg = "Cannot roll log file %s to fix log " "filename conflicts";
            Error(msg, filename);
            LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename);
            retVal = CANNOT_SOLVE_FILENAME_CONFLICTS;

          }
        }
      }
    }
  }
#endif // TS_MICRO
  return retVal;
}

#ifndef TS_MICRO
bool
  LogObjectManager::_has_internal_filename_conflict(char *filename,
                                                    inku64 signature, LogObject ** objects, int numObjects)
{
  for (int i = 0; i < numObjects; i++) {
    LogObject *obj = objects[i];
    if (!obj->is_collation_client()) {
      // an internal conflict exists if two objects request the 
      // same filename, regardless of the object signatures, since
      // two objects writing to the same file would produce a 
      // log with duplicate entries and non monotonic timestamps
      if (strcmp(obj->get_full_filename(), filename) == 0) {
        return true;
      }
    }
  }
  return false;
}
#endif // TS_MICRO

int
LogObjectManager::_solve_internal_filename_conflicts(LogObject * log_object, int maxConflicts, int fileNum)
{
  int retVal = NO_FILENAME_CONFLICTS;

#ifndef TS_MICRO
  char *filename = log_object->get_full_filename();
  inku64 signature = log_object->get_signature();

  if (_has_internal_filename_conflict(filename, signature,
                                      _objects, _numObjects) ||
      _has_internal_filename_conflict(filename, signature, _APIobjects, _numAPIobjects)) {
    if (fileNum < maxConflicts) {
      char new_name[MAXPATHLEN];
      snprintf(new_name, sizeof(new_name), "%s%s%d",
               log_object->get_original_filename(), LOGFILE_SEPARATOR_STRING, ++fileNum);
      log_object->rename(new_name);
      retVal = _solve_internal_filename_conflicts(log_object, maxConflicts, fileNum);
    } else {
      char *msg = "Cannot solve filename conflicts for log file %s";
      Error(msg, filename);
      LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename);
      retVal = CANNOT_SOLVE_FILENAME_CONFLICTS;
    }
  }
#endif // TS_MICRO
  return retVal;
}

int
LogObjectManager::_roll_files(long time_now, bool roll_only_if_needed)
{
  int num_rolled = 0;
  size_t i;
  for (i = 0; i < _numObjects; i++) {
    if (roll_only_if_needed) {
      num_rolled += _objects[i]->roll_files_if_needed(time_now);
    } else {
      num_rolled += _objects[i]->roll_files(time_now);
    }
  }
  // we don't care if we miss an object that may be added to the set of api 
  // objects just after we have read _numAPIobjects and found it to be zero; 
  // we will get a chance to roll this object next time
  // 
  if (_numAPIobjects) {
    ACQUIRE_API_MUTEX("A LogObjectManager::roll_files");
    for (i = 0; i < _numAPIobjects; i++) {
      if (roll_only_if_needed) {
        num_rolled += _APIobjects[i]->roll_files_if_needed(time_now);
      } else {
        num_rolled += _APIobjects[i]->roll_files(time_now);
      }
    }
    RELEASE_API_MUTEX("R LogObjectManager::roll_files");
  }
  return num_rolled;
};

LogObject *
LogObjectManager::get_object_with_signature(inku64 signature)
{
  for (size_t i = 0; i < _numObjects; i++) {
    LogObject *obj = _objects[i];
    if (obj->get_signature() == signature) {
      return obj;
    }
  }
  return (LogObject *) (0);
}

void
LogObjectManager::check_buffer_expiration(long time_now)
{
  size_t i;
  for (i = 0; i < _numObjects; i++) {
    _objects[i]->check_buffer_expiration(time_now);
  }

  // we don't care if we miss an object that may be added to the set of api 
  // objects just after we have read _numAPIobjects and found it to be zero; 
  // we will get a chance to check the buffer expiration next time 
  // 
  if (_numAPIobjects) {
    ACQUIRE_API_MUTEX("A LogObjectManager::check_buffer_expiration");
    for (i = 0; i < _numAPIobjects; i++) {
      _APIobjects[i]->check_buffer_expiration(time_now);
    }
    RELEASE_API_MUTEX("R LogObjectManager::check_buffer_expiration");
  }
}

size_t LogObjectManager::flush_buffers(size_t * to_disk, size_t * to_net, size_t * to_pipe)
{
  size_t
    i;
  size_t
    bytes_flushed;
  size_t
    total_bytes_flushed = 0;

  for (i = 0; i < _numObjects; i++) {
    LogObject *
      obj = _objects[i];
    bytes_flushed = obj->flush_buffers(to_disk, to_net, to_pipe);
    total_bytes_flushed += bytes_flushed;
  }

  // we don't care if we miss an object that may be added to the set of
  // api objects just after we have read _numAPIobjects and found it to
  // be zero; we will get a chance to flush the buffer next time
  //
  if (_numAPIobjects) {
    ACQUIRE_API_MUTEX("A LogObjectManager::flush_buffers");
    for (i = 0; i < _numAPIobjects; i++) {
      bytes_flushed = _APIobjects[i]->flush_buffers(to_disk, to_net, to_pipe);
      total_bytes_flushed += bytes_flushed;
    }
    RELEASE_API_MUTEX("R LogObjectManager::flush_buffers");
  }
  return total_bytes_flushed;
}


int
LogObjectManager::unmanage_api_object(LogObject * logObject)
{
  ACQUIRE_API_MUTEX("A LogObjectManager::unmanage_api_object");

  for (size_t i = 0; i < _numAPIobjects; i++) {
    if (logObject == _APIobjects[i]) {

      Log::add_to_inactive(logObject);
      logObject->force_new_buffer();

      for (size_t j = i + 1; j < _numAPIobjects; j++) {
        _APIobjects[j - 1] = _APIobjects[j];
      }

      --_numAPIobjects;
      RELEASE_API_MUTEX("R LogObjectManager::unmanage_api_object");
      return 1;
    }
  }
  RELEASE_API_MUTEX("R LogObjectManager::unmanage_api_object");
  return 0;
};

void
LogObjectManager::add_filter_to_all(LogFilter * filter)
{
  for (size_t i = 0; i < _numObjects; i++) {
    _objects[i]->add_filter(filter);
  }
}

void
LogObjectManager::open_local_pipes()
{
  // for all local objects that write to a pipe, call open_file to force 
  // the creation of the pipe so that any potential reader can see it
  //
  for (size_t i = 0; i < _numObjects; i++) {
    LogObject *obj = _objects[i];
    if (obj->writes_to_pipe() && !obj->is_collation_client()) {
      obj->m_logFile->open_file();
    }
  }
}


void
LogObjectManager::transfer_objects(LogObjectManager & old_mgr)
{
  LogObject *old_obj, *obj;
  size_t i;
  size_t num_kept_objects = 0;

  if (is_debug_tag_set("log2-config-transfer")) {
    Debug("log2-config-transfer", "TRANSFER OBJECTS: list of old objects");
    for (i = 0; i < old_mgr._numObjects; i++) {
      Debug("log2-config-transfer", "%s", old_mgr._objects[i]->get_original_filename());
    }

    Debug("log2-config-transfer", "TRANSFER OBJECTS : list of new objects");
    for (i = 0; i < _numObjects; i++) {
      Debug("log2-config-transfer", "%s", _objects[i]->get_original_filename());
    }
  }

  for (i = 0; i < old_mgr._numAPIobjects; i++) {
    _add_api_object(old_mgr._APIobjects[i]);
  }

  LogObject **old_objects = old_mgr._objects;
  for (i = 0; i < old_mgr._numObjects; i++) {
    old_obj = old_objects[i];

    Debug("log2-config-transfer", "examining existing object %s", old_obj->get_base_filename());

    // see if any of the new objects is just a copy of an old one, 
    // if so, keep the old one and delete the new one
    //
    size_t j = _numObjects;

    if (num_kept_objects < _numObjects) {
      for (j = 0; j < _numObjects; j++) {
        obj = _objects[j];

        Debug("log2-config-transfer",
              "comparing existing object %s to new object %s", old_obj->get_base_filename(), obj->get_base_filename());

        if (*obj == *old_obj) {
          Debug("log2-config-transfer", "keeping existing object %s", old_obj->get_base_filename());

          _objects[j] = old_obj;
          delete obj;
          ++num_kept_objects;
          break;
        }
      }
    }
    // if old object is not in the new list, move it to list of
    // inactive objects
    //
    if (j == _numObjects) {
      Debug("log2-config-transfer", "moving existing object %s to inactive list", old_obj->get_base_filename());

      Log::add_to_inactive(old_obj);
    }
  }

  if (is_debug_tag_set("log2-config-transfer")) {
    Debug("log2-config-transfer", "Log Object List after transfer:");
    display();
  }
}
