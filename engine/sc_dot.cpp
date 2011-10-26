// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simulationcraft.h"

// ==========================================================================
// Dot
// ==========================================================================

dot_t::dot_t( const std::string& n, player_t* p ) :
  sim( p -> sim ), player( p ), action( 0 ), name_str( n ), tick_event( 0 ),
  num_ticks( 0 ), current_tick( 0 ), added_ticks( 0 ), ticking( 0 ),
  added_seconds( 0.0 ), ready( -1.0 ), miss_time( -1.0 ),time_to_tick( 0.0 ), next( 0 )
{}

dot_t::~dot_t()
{}

// dot_t::extend_duration ===================================================

void dot_t::extend_duration( int extra_ticks, bool cap )
{
  if ( ! ticking )
    return;

  // Make sure this DoT is still ticking......
  assert( tick_event );

  if ( sim -> log )
    log_t::output( sim, "%s extends duration of %s, adding %d tick(s), totalling %d ticks", player -> name(), name(), extra_ticks, num_ticks + extra_ticks );

  if ( cap )
  {
    // Can't extend beyond initial duration.
    // Assuming this limit is based on current haste, not haste at previous application/extension/refresh.

    int max_extra_ticks = std::max( action -> hasted_num_ticks() - ticks(), 0 );

    extra_ticks = std::min( extra_ticks, max_extra_ticks );
  }

  action -> player_buff();

  added_ticks += extra_ticks;
  num_ticks += extra_ticks;
  recalculate_ready();
}

// dot_t::extend_duration_seconds ===========================================

void dot_t::extend_duration_seconds( double extra_seconds )
{
  if ( ! ticking )
    return;

  // Make sure this DoT is still ticking......
  assert( tick_event );

  // Treat extra_ticks as 'seconds added' instead of 'ticks added'
  // Duration left needs to be calculated with old haste for tick_time()
  // First we need the number of ticks remaining after the next one =>
  // ( num_ticks - current_tick ) - 1
  int old_num_ticks = num_ticks;
  int old_remaining_ticks = old_num_ticks - current_tick - 1;
  double old_haste_factor = 1.0 / action -> player_haste;

  // Multiply with tick_time() for the duration left after the next tick
  double duration_left = old_remaining_ticks * action -> tick_time();

  // Add the added seconds
  duration_left += extra_seconds;

  action -> player_buff();

  added_seconds += extra_seconds;

  int new_remaining_ticks = action -> hasted_num_ticks( duration_left );
  num_ticks += ( new_remaining_ticks - old_remaining_ticks );

  if ( sim -> debug )
  {
    log_t::output( sim, "%s extends duration of %s by %.1f second(s). h: %.2f => %.2f, num_t: %d => %d, rem_t: %d => %d",
                   player -> name(), name(), extra_seconds,
                   old_haste_factor, ( 1.0 / action -> player_haste ),
                   old_num_ticks, num_ticks,
                   old_remaining_ticks, new_remaining_ticks );
  }
  else if ( sim -> log )
  {
    log_t::output( sim, "%s extends duration of %s by %.1f second(s).", player -> name(), name(), extra_seconds );
  }

  recalculate_ready();
}

// dot_t::recalculate_ready =================================================

void dot_t::recalculate_ready()
{
  // Extending a DoT does not interfere with the next tick event.  To determine the
  // new finish time for the DoT, start from the time of the next tick and add the time
  // for the remaining ticks to that event.
  int remaining_ticks = num_ticks - current_tick;
  ready = 0.001 + tick_event -> time + action -> tick_time() * ( remaining_ticks - 1 );
}

// dot_t::refresh_duration ==================================================

void dot_t::refresh_duration()
{
  if ( ! ticking )
    return;

  // Make sure this DoT is still ticking......
  assert( tick_event );

  if ( sim -> log )
    log_t::output( sim, "%s refreshes duration of %s", player -> name(), name() );

  action -> player_buff();

  current_tick = 0;
  added_ticks = 0;
  added_seconds = 0;
  num_ticks = action -> hasted_num_ticks();
  recalculate_ready();
}

// dot_t::remains ===========================================================

double dot_t::remains()
{
  if ( ! action ) return 0;
  if ( ! ticking ) return 0;
  return ready - player -> sim -> current_time;
}

// dot_t::reset =============================================================

void dot_t::reset()
{
  tick_event=0;
  current_tick=0;
  added_ticks=0;
  ticking=0;
  added_seconds=0.0;
  ready=-1;
  miss_time=-1;
}

// dot_t::schedule_tick =====================================================

void dot_t::schedule_tick()
{
  if ( sim -> debug )
    log_t::output( sim, "%s schedules tick for %s", player -> name(), name() );

  if ( current_tick == 0 )
  {

    if ( action -> tick_zero )
    {
      time_to_tick = 0;
      action -> tick( this );
    }
  }

  time_to_tick = action -> tick_time();

  tick_event = new ( sim ) dot_tick_event_t( sim, this, time_to_tick );

  ticking = 1;

  if ( action -> channeled ) player -> channeling = action;
}

// dot_t::ticks =============================================================

int dot_t::ticks()
{
  if ( ! action ) return 0;
  if ( ! ticking ) return 0;
  return ( num_ticks - current_tick );
}