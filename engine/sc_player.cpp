// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simulationcraft.h"

namespace { // ANONYMOUS NAMESPACE ==========================================

// dark_intent_callback ==================================================

struct dark_intent_callback_t : public action_callback_t
{
  dark_intent_callback_t( player_t* p ) : action_callback_t( p -> sim, p ) {}

  virtual void trigger( action_t* a, void* call_data )
  {
    listener -> buffs.dark_intent_feedback -> trigger();
  }
};

// hymn_of_hope_buff ========================================================

struct hymn_of_hope_buff_t : public buff_t
{
  double mana_gain;
  hymn_of_hope_buff_t( player_t* p, const uint32_t id, const std::string& n ) :
    buff_t ( p, id, n )
  {
    mana_gain = 0;

  }
  virtual void start( int    stacks,
                      double value )
  {
    buff_t::start( stacks, value );

    // Extra Mana is only added at the start, not on refresh. Tested 20/01/2011.
    // Extra Mana is set by current max_mana, doesn't change when max_mana changes.
    mana_gain = player -> resource_max[ RESOURCE_MANA ] * 0.15;
    player -> stat_gain( STAT_MAX_MANA, mana_gain, player -> gains.hymn_of_hope );
  }

  virtual void expire()
  {
    buff_t::expire();
    player -> stat_loss( STAT_MAX_MANA, mana_gain );
  }
};

// Event Vengeance

struct vengeance_t : public event_t
{
  vengeance_t ( player_t* player ) :
    event_t( player -> sim, player )
  {
    name = "Vengeance_Check";
    sim -> add_event( this, 2.0 );
  }

  virtual void execute()
  {
    if ( player -> vengeance_damage > 1 )
    {
      player -> vengeance_value *= 0.95;
      player -> vengeance_value += 0.05 * player -> vengeance_damage;
    }
    else
    {
      player -> vengeance_value -= 0.1 * player -> vengeance_max;
    }

    if ( player -> vengeance_value < 0 )
      player -> vengeance_value = 0;

    if ( player -> vengeance_value > ( player -> stamina() + 0.1 * player -> resource_base[ RESOURCE_HEALTH ] ) )
      player -> vengeance_value = ( player -> stamina() + 0.1 * player -> resource_base[ RESOURCE_HEALTH ] );

    if ( player -> vengeance_value > player -> vengeance_max )
      player -> vengeance_max = player -> vengeance_value;


    if ( sim -> debug )
    {
      log_t::output( sim, "%s updated vengeance. New vengeance_value=%.2f and vengeance_max=%.2f. vengeance_damage=%.2f.\n",
                     player -> name(), player -> vengeance_value,
                     player -> vengeance_max, player -> vengeance_damage );
    }

    player -> vengeance_damage = 0;

    new ( sim ) vengeance_t( player );
  }
};

// has_foreground_actions ================================================

static bool has_foreground_actions( player_t* p )
{
  for ( action_t* a = p -> action_list; a; a = a -> next )
  {
    if ( ! a -> background ) return true;
  }
  return false;
}

// init_replenish_targets ================================================

static void init_replenish_targets( sim_t* sim )
{
  if ( sim -> replenishment_candidates.empty() )
  {
    for ( player_t* p = sim -> player_list; p; p = p -> next )
    {
      if ( p -> type != PLAYER_GUARDIAN &&
           p -> primary_resource() == RESOURCE_MANA )
      {
        sim -> replenishment_candidates.push_back( p );
      }
    }
  }
}

// choose_replenish_targets ==============================================

static void choose_replenish_targets( player_t* provider )
{
  sim_t* sim = provider -> sim;

  init_replenish_targets( sim );

  std::vector<player_t*>& candidates = sim      -> replenishment_candidates;
  std::vector<player_t*>& targets    = provider -> replenishment_targets;

  bool replenishment_missing = false;

  for ( int i = ( int ) candidates.size() - 1; i >= 0; i-- )
  {
    player_t* p = candidates[ i ];

    if ( ! p -> sleeping && ! p -> buffs.replenishment -> check() )
    {
      replenishment_missing = true;
      break;
    }
  }

  // If replenishment is not missing from any of the candidates, then the target list will not change

  if ( ! replenishment_missing ) return;

  for ( int i = ( int ) targets.size() - 1; i >= 0; i-- )
  {
    targets[ i ] -> buffs.replenishment -> current_value = -1.0;
  }
  targets.clear();

  for ( int i=0; i < sim -> replenishment_targets; i++ )
  {
    player_t* min_player=0;
    double    min_mana=0;

    for ( int j = ( int ) candidates.size() - 1; j >= 0; j-- )
    {
      player_t* p = candidates[ j ];

      if ( p -> sleeping || ( p -> buffs.replenishment -> current_value == 1.0 ) ) continue;

      if ( ! min_player || min_mana > p -> resource_current[ RESOURCE_MANA ] )
      {
        min_player = p;
        min_mana   = p -> resource_current[ RESOURCE_MANA ];
      }
    }
    if ( min_player )
    {
      min_player -> buffs.replenishment -> trigger( 1, 1.0 );
      targets.push_back( min_player );
    }
    else break;
  }

  for ( int i = ( int ) candidates.size() - 1; i >= 0; i-- )
  {
    player_t* p = candidates[ i ];

    if ( p -> buffs.replenishment -> current_value < 0 )
    {
      p -> buffs.replenishment -> expire();
    }
  }
}

// replenish_raid ========================================================

static void replenish_raid( player_t* provider )
{
  for ( player_t* p = provider -> sim -> player_list; p; p = p -> next )
  {
    p -> buffs.replenishment -> trigger();
  }
}

// parse_talent_url =========================================================

static bool parse_talent_url( sim_t* sim,
                              const std::string& name,
                              const std::string& url )
{
  assert( name == "talents" );

  player_t* p = sim -> active_player;

  p -> talents_str = url;

  std::string::size_type cut_pt;

  if ( url.find( "worldofwarcraft" ) != url.npos )
  {
    if ( ( cut_pt = url.find_first_of( "=" ) ) != url.npos )
    {
      return p -> parse_talents_armory( url.substr( cut_pt + 1 ) );
    }
  }
  else if ( url.find( "wowarmory" ) != url.npos )
  {
    if ( ( cut_pt = url.find_last_of( "=" ) ) != url.npos )
    {
      return p -> parse_talents_armory( url.substr( cut_pt + 1 ) );
    }
  }
  else if ( url.find( "wowhead" ) != url.npos )
  {
    if ( ( cut_pt = url.find_first_of( "#=" ) ) != url.npos )
    {
      std::string::size_type cut_pt2 = url.find_first_of( "-", cut_pt + 1 );
      // Add support for http://www.wowhead.com/talent#priest-033211000000000000000000000000000000000000322032210201222100231
      if ( cut_pt2 != url.npos )
        return p -> parse_talents_armory( url.substr( cut_pt2 + 1 ) );
      else
        return p -> parse_talents_wowhead( url.substr( cut_pt + 1 ) );
    }
  }
  else
  {
    bool all_digits = true;
    for( int i=url.size()-1; i >= 0 && all_digits; i-- )
      if( ! isdigit( url[ i ] ) )
        all_digits = false;

    if( all_digits )
    {
      return p -> parse_talents_armory( url );
    }
  }

  sim -> errorf( "Unable to decode talent string %s for %s\n", url.c_str(), p -> name() );

  return false;
}

// parse_role_string ========================================================

static bool parse_role_string( sim_t* sim,
                               const std::string& name,
                               const std::string& value )
{
  assert( name == "role" );

  sim -> active_player -> role = util_t::parse_role_type( value );

  return true;
}


// parse_world_lag ========================================================

static bool parse_world_lag( sim_t* sim,
                               const std::string& name,
                               const std::string& value )
{
  assert( name == "world_lag" );

  sim -> active_player -> world_lag = atof( value.c_str() );

  if ( sim -> active_player -> world_lag < 0.0 )
  {
    sim -> active_player -> world_lag = 0.0;
  }

  sim -> active_player -> world_lag_override = true;

  return true;
}


// parse_world_lag ========================================================

static bool parse_world_lag_stddev( sim_t* sim,
                               const std::string& name,
                               const std::string& value )
{
  assert( name == "world_lag_stddev" );

  sim -> active_player -> world_lag_stddev = atof( value.c_str() );

  if ( sim -> active_player -> world_lag_stddev < 0.0 )
  {
    sim -> active_player -> world_lag_stddev = 0.0;
  }

  sim -> active_player -> world_lag_stddev_override = true;

  return true;
}

// parse_brain_lag ========================================================

static bool parse_brain_lag( sim_t* sim,
                               const std::string& name,
                               const std::string& value )
{
  assert( name == "brain_lag" );

  sim -> active_player -> brain_lag = atof( value.c_str() );

  if ( sim -> active_player -> brain_lag < 0.0 )
  {
    sim -> active_player -> brain_lag = 0.0;
  }

  return true;
}


// parse_brain_lag_stddev ========================================================

static bool parse_brain_lag_stddev( sim_t* sim,
                               const std::string& name,
                               const std::string& value )
{
  assert( name == "brain_lag_stddev" );

  sim -> active_player -> brain_lag_stddev = atof( value.c_str() );

  if ( sim -> active_player -> brain_lag_stddev < 0.0 )
  {
    sim -> active_player -> brain_lag_stddev = 0.0;
  }

  return true;
}

} // ANONYMOUS NAMESPACE ===================================================


// ==========================================================================
// Player
// ==========================================================================

// player_t::player_t =======================================================

player_t::player_t( sim_t*             s,
                    player_type        t,
                    const std::string& n,
                    race_type          r ) :
  sim( s ), ptr( s -> dbc.ptr ), name_str( n ), target_str( "" ),
  region_str( s->default_region_str ), server_str( s->default_server_str ), origin_str( "unknown" ),
  next( 0 ), index( -1 ), type( t ), role( ROLE_HYBRID ), target( NULL ), level( is_enemy() ? 88 : 85 ), use_pre_potion( 1 ),
  party( 0 ), member( 0 ),
  skill( 0 ), initial_skill( s->default_skill ), distance( 0 ), gcd_ready( 0 ), base_gcd( 1.5 ),
  potion_used( 0 ), sleeping( 1 ), initialized( 0 ),
  pet_list( 0 ), last_modified( 0 ), bugs( true ), specialization( TALENT_TAB_NONE ), invert_scaling( 0 ),
  vengeance_enabled( false ), vengeance_damage( 0.0 ), vengeance_value( 0.0 ), vengeance_max( 0.0 ),
  active_pets( 0 ), big_hitbox( 0 ), dtr_proc_chance( -1.0 ), dtr_base_proc_chance( -1.0 ),
  reaction_mean( 0.5 ), reaction_stddev( 0.0 ), reaction_nu( 0.5 ),
  // Latency
  world_lag( 0.1 ), world_lag_stddev( -1.0 ),
  brain_lag( -1.0 ), brain_lag_stddev( -1.0 ),
  world_lag_override( false ), world_lag_stddev_override( false ),
  dbc( s -> dbc ),
  race_str( "" ), race( r ),
  // Haste
  base_haste_rating( 0 ), initial_haste_rating( 0 ), haste_rating( 0 ),
  spell_haste( 1.0 ),  buffed_spell_haste( 1.0 ),
  attack_haste( 1.0 ), buffed_attack_haste( 1.0 ),
  // Mastery
  mastery( 0 ), buffed_mastery ( 0 ), mastery_rating( 0 ), initial_mastery_rating ( 0 ), base_mastery ( 8.0 ),
  // Spell Mechanics
  base_spell_power( 0 ), buffed_spell_power( 0 ),
  base_spell_hit( 0 ),         initial_spell_hit( 0 ),         spell_hit( 0 ),         buffed_spell_hit( 0 ),
  base_spell_crit( 0 ),        initial_spell_crit( 0 ),        spell_crit( 0 ),        buffed_spell_crit( 0 ),
  base_spell_penetration( 0 ), initial_spell_penetration( 0 ), spell_penetration( 0 ), buffed_spell_penetration( 0 ),
  base_mp5( 0 ),               initial_mp5( 0 ),               mp5( 0 ),               buffed_mp5( 0 ),
  spell_power_multiplier( 1.0 ),  initial_spell_power_multiplier( 1.0 ),
  spell_power_per_intellect( 0 ), initial_spell_power_per_intellect( 0 ),
  spell_power_per_spirit( 0 ),    initial_spell_power_per_spirit( 0 ),
  spell_crit_per_intellect( 0 ),  initial_spell_crit_per_intellect( 0 ),
  mp5_per_intellect( 0 ),
  mana_regen_base( 0 ), mana_regen_while_casting( 0 ),
  base_energy_regen_per_second( 0 ), base_focus_regen_per_second( 0 ),
  last_cast( 0 ),
  // Attack Mechanics
  base_attack_power( 0 ),       initial_attack_power( 0 ),        attack_power( 0 ),       buffed_attack_power( 0 ),
  base_attack_hit( 0 ),         initial_attack_hit( 0 ),          attack_hit( 0 ),         buffed_attack_hit( 0 ),
  base_attack_expertise( 0 ),   initial_attack_expertise( 0 ),    attack_expertise( 0 ),   buffed_attack_expertise( 0 ),
  base_attack_crit( 0 ),        initial_attack_crit( 0 ),         attack_crit( 0 ),        buffed_attack_crit( 0 ),
  attack_power_multiplier( 1.0 ), initial_attack_power_multiplier( 1.0 ),
  attack_power_per_strength( 0 ), initial_attack_power_per_strength( 0 ),
  attack_power_per_agility( 0 ),  initial_attack_power_per_agility( 0 ),
  attack_crit_per_agility( 0 ),   initial_attack_crit_per_agility( 0 ),
  position( POSITION_BACK ),
  // Defense Mechanics
  target_auto_attack( 0 ),
  base_armor( 0 ),       initial_armor( 0 ),       armor( 0 ),       buffed_armor( 0 ),
  base_bonus_armor( 0 ), initial_bonus_armor( 0 ), bonus_armor( 0 ),
  base_miss( 0 ),        initial_miss( 0 ),        miss( 0 ),        buffed_miss( 0 ), buffed_crit( 0 ),
  base_dodge( 0 ),       initial_dodge( 0 ),       dodge( 0 ),       buffed_dodge( 0 ),
  base_parry( 0 ),       initial_parry( 0 ),       parry( 0 ),       buffed_parry( 0 ),
  base_block( 0 ),       initial_block( 0 ),       block( 0 ),       buffed_block( 0 ),
  armor_multiplier( 1.0 ), initial_armor_multiplier( 1.0 ),
  dodge_per_agility( 0 ), initial_dodge_per_agility( 0 ),
  parry_rating_per_strength( 0 ), initial_parry_rating_per_strength( 0 ),
  diminished_dodge_capi( 0 ), diminished_parry_capi( 0 ), diminished_kfactor( 0 ),
  armor_coeff( 0 ),
  half_resistance_rating( 0 ),
  // Attacks
  main_hand_attack( 0 ), off_hand_attack( 0 ), ranged_attack( 0 ),
  // Resources
  mana_per_intellect( 0 ), health_per_stamina( 0 ),
  // Consumables
  elixir_guardian( ELIXIR_NONE ),
  elixir_battle( ELIXIR_NONE ),
  flask( FLASK_NONE ),
  food( FOOD_NONE ),
  // Events
  executing( 0 ), channeling( 0 ), readying( 0 ), in_combat( false ), action_queued( false ),
  cast_delay_reaction( 0 ), cast_delay_occurred( 0 ),
  // Actions
  action_list( 0 ), action_list_default( 0 ), cooldown_list( 0 ), dot_list( 0 ),
  // Reporting
  quiet( 0 ), last_foreground_action( 0 ),
  current_time( 0 ), total_seconds( 0 ),
  total_waiting( 0 ), total_foreground_actions( 0 ),
  iteration_dmg( 0 ), total_dmg( 0 ),
  dps( 0 ), dps_min( 0 ), dps_max( 0 ),
  dps_std_dev( 0 ), dps_error( 0 ), dps_convergence( 0 ),
  dps_10_percentile( 0 ),dps_90_percentile( 0 ),
  dpr( 0 ), rps_gain( 0 ), rps_loss( 0 ),
  death_count( 0 ), avg_death_time( 0.0 ), death_count_pct( 0.0 ), min_death_time( FLT_MAX ),
  dmg_taken( 0.0 ), total_dmg_taken( 0.0 ),
  buff_list( 0 ), proc_list( 0 ), gain_list( 0 ), stats_list( 0 ), uptime_list( 0 ),
  save_str( "" ), save_gear_str( "" ), save_talents_str( "" ), save_actions_str( "" ),
  comment_str( "" ),
  // Gear
  sets( 0 ),
  meta_gem( META_GEM_NONE ), matching_gear( false ),
  // Scaling
  scaling_lag( 0 ),
  // Movement & Position
  base_movement_speed( 7.0 ), x_position( 0.0 ), y_position( 0.0 ),

  rng_list( 0 )
{
  sim -> actor_list.push_back( this );

  if ( type != ENEMY && type != ENEMY_ADD )
  {
    if ( sim -> debug ) log_t::output( sim, "Creating Player %s", name() );
    player_t** last = &( sim -> player_list );
    while ( *last ) last = &( ( *last ) -> next );
    *last = this;
    next = 0;
    index = ++( sim -> num_players );
  }
  else
  {
    index = - ( ++( sim -> num_enemies ) );
  }

  race_str = util_t::race_type_string( race );

  if ( is_pet() ) skill = 1.0;

  for ( int i=0; i < SCHOOL_MAX; i++ ) spell_resistance[ i ] = 0;

  for ( int i=0; i < ATTRIBUTE_MAX; i++ )
  {
    attribute[ i ] = attribute_base[ i ] = attribute_initial[ i ] = 0;
    attribute_multiplier[ i ] = attribute_multiplier_initial[ i ] = 1.0;
    attribute_buffed[ i ] = 0;
  }

  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    infinite_resource[ i ] = false;
  }
  infinite_resource[ RESOURCE_HEALTH ] = true;

  for ( int i=0; i <= SCHOOL_MAX; i++ )
  {
    initial_spell_power[ i ] = spell_power[ i ] = 0;
    initial_resource_reduction[ i ] = resource_reduction[ i ] = 0;
  }

  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    resource_base[ i ] = resource_initial[ i ] = resource_max[ i ] = resource_current[ i ] = 0;
    resource_lost[ i ] = resource_gained[ i ] = 0;
    resource_buffed[ i ] = 0;
  }

  for ( int i=0; i < PROFESSION_MAX; i++ ) profession[ i ] = 0;
  for ( int i=0; i < STAT_MAX; i++ )
  {
    scales_with[ i ] = 0;
    over_cap[ i ] = 0;
  }

  items.resize( SLOT_MAX );
  for ( int i=0; i < SLOT_MAX; i++ )
  {
    items[ i ].slot = i;
    items[ i ].sim = sim;
    items[ i ].player = this;
  }

  main_hand_weapon.slot = SLOT_MAIN_HAND;
  off_hand_weapon.slot = SLOT_OFF_HAND;
  ranged_weapon.slot = SLOT_RANGED;

  if ( ! sim -> active_files.empty() ) origin_str = sim -> active_files.back();

  for ( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    talent_tab_points[ i ] = 0;
    tree_type[ i ] = TREE_NONE;
  }

  if ( reaction_stddev == 0 ) reaction_stddev =   reaction_mean * 0.25;
}

// player_t::~player_t =====================================================

player_t::~player_t()
{
  while ( action_t* a = action_list )
  {
    action_list = a -> next;
    delete a;
  }
  while ( proc_t* p = proc_list )
  {
    proc_list = p -> next;
    delete p;
  }
  while ( gain_t* g = gain_list )
  {
    gain_list = g -> next;
    delete g;
  }
  while ( stats_t* s = stats_list )
  {
    stats_list = s -> next;
    delete s;
  }
  while ( uptime_t* u = uptime_list )
  {
    uptime_list = u -> next;
    delete u;
  }
  while ( rng_t* r = rng_list )
  {
    rng_list = r -> next;
    delete r;
  }
  while ( dot_t* d = dot_list )
  {
    dot_list = d -> next;
    delete d;
  }
  while ( buff_t* d = buff_list )
  {
    buff_list = d -> next;
    delete d;
  }
  while ( cooldown_t* d = cooldown_list )
  {
    cooldown_list = d -> next;
    delete d;
  }
  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    resource_gain_callbacks[ i ].clear();
    resource_loss_callbacks[ i ].clear();
  }
  for ( int i=0; i < RESULT_MAX; i++ )
  {
    attack_callbacks[ i ].clear();
    spell_callbacks[ i ].clear();
    tick_callbacks[ i ].clear();
  }
  for ( int i=0; i < SCHOOL_MAX; i++ )
  {
    tick_damage_callbacks[ i ].clear();
    tick_heal_callbacks  [ i ].clear();

    direct_damage_callbacks[ i ].clear();
    direct_heal_callbacks  [ i ].clear();
  }

  if ( false )
  {
    // FIXME! This cannot be done until we use refcounts.
    // FIXME! I see the same callback pointer being registered multiple times.
    for( int i=all_callbacks.size()-1; i >= 0; i-- )
      delete all_callbacks[ i ];
  }
  all_callbacks.clear();

  for( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    for( int j=talent_trees[ i ].size()-1; j >= 0; j-- )
      delete talent_trees[ i ][ j ];
    talent_trees[ i ].clear();
  }

  for( int i=glyphs.size()-1; i >= 0; i-- )
    delete glyphs[ i ];
  glyphs.clear();

  while ( spell_list.size() )
  {
    spell_id_t* s = spell_list.back();
    spell_list.pop_back();
    delete s;
  }
  if ( sets )
    delete sets;
}

// player_t::init ==========================================================

bool player_t::init( sim_t* sim )
{
  if ( sim -> debug )
    log_t::output( sim, "Creating Pets." );

  for ( unsigned int i = 0; i < sim -> actor_list.size(); i++ )
  {
    player_t* p = sim -> actor_list[i];
    p -> create_pets();
  }

  if ( sim -> debug )
    log_t::output( sim, "Initializing Auras, Buffs, and De-Buffs." );

  player_t::death_knight_init( sim );
  player_t::druid_init       ( sim );
  player_t::hunter_init      ( sim );
  player_t::mage_init        ( sim );
  player_t::paladin_init     ( sim );
  player_t::priest_init      ( sim );
  player_t::rogue_init       ( sim );
  player_t::shaman_init      ( sim );
  player_t::warlock_init     ( sim );
  player_t::warrior_init     ( sim );
  player_t::enemy_init       ( sim );

  if ( sim -> debug )
    log_t::output( sim, "Initializing Players." );

  bool too_quiet = true; // Check for at least 1 active player
  bool zero_dds = true; // Check for at least 1 player != TANK/HEAL

  for ( unsigned int i = 0; i < sim -> actor_list.size(); i++ )
  {
    player_t* p = sim -> actor_list[i];
    if ( sim -> default_actions && ! p -> is_pet() ) p -> action_list_str.clear();
    p -> init();
    if ( ! p -> quiet ) too_quiet = false;
    if ( p -> primary_role() != ROLE_HEAL && p -> primary_role() != ROLE_TANK && !p -> is_pet() ) zero_dds = false;
  }

  if ( too_quiet && ! sim -> debug )
  {
    sim -> errorf( "No active players in sim!" );
    return false;
  }

  // Set Fixed_Time when there are no DD's present
  if ( zero_dds && ! sim -> debug )
    sim -> fixed_time = true;


  // Parties
  if ( sim -> debug )
    log_t::output( sim, "Building Parties." );

  int party_index=0;
  for ( unsigned i=0; i < sim -> party_encoding.size(); i++ )
  {
    std::string& party_str = sim -> party_encoding[ i ];

    if ( party_str == "reset" )
    {
      party_index = 0;
      for ( player_t* p = sim -> player_list; p; p = p -> next ) p -> party = 0;
    }
    else if ( party_str == "all" )
    {
      int member_index = 0;
      for ( player_t* p = sim -> player_list; p; p = p -> next )
      {
        p -> party = 1;
        p -> member = member_index++;
      }
    }
    else
    {
      party_index++;

      std::vector<std::string> player_names;
      int num_players = util_t::string_split( player_names, party_str, ",;/" );
      int member_index=0;

      for ( int j=0; j < num_players; j++ )
      {
        player_t* p = sim -> find_player( player_names[ j ] );
        if ( ! p )
        {
          sim -> errorf( "Unable to find player %s for party creation.\n", player_names[ j ].c_str() );
          return false;
        }
        p -> party = party_index;
        p -> member = member_index++;
        for ( pet_t* pet = p -> pet_list; pet; pet = pet -> next_pet )
        {
          pet -> party = party_index;
          pet -> member = member_index++;
        }
      }
    }
  }

  // Callbacks
  if ( sim -> debug )
    log_t::output( sim, "Registering Callbacks." );

  for ( unsigned int i = 0; i < sim -> actor_list.size(); i++ )
  {
    player_t* p = sim -> actor_list[i];
    p -> register_callbacks();
  }

  return true;
}

// player_t::init ==========================================================

void player_t::init()
{
  if ( sim -> debug ) log_t::output( sim, "Initializing player %s", name() );

  initialized = 1;
  init_target();
  init_talents();
  init_spells();
  init_glyphs();
  init_rating();
  init_race();
  init_base();
  init_racials();
  init_items();
  init_core();
  init_spell();
  init_attack();
  init_defense();
  init_weapon( &main_hand_weapon );
  init_weapon( &off_hand_weapon );
  init_weapon( &ranged_weapon );
  init_unique_gear();
  init_enchant();
  init_professions();
  init_consumables();
  init_scaling();
  init_buffs();
  init_values();
  init_actions();
  init_gains();
  init_procs();
  init_uptimes();
  init_rng();
  init_stats();
}

// player_t::init_base =====================================================

void player_t::init_base()
{
  attribute_base[ ATTR_STRENGTH  ] = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_STRENGTH );
  attribute_base[ ATTR_AGILITY   ] = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_AGILITY );
  attribute_base[ ATTR_STAMINA   ] = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_STAMINA );
  attribute_base[ ATTR_INTELLECT ] = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_INTELLECT );
  attribute_base[ ATTR_SPIRIT    ] = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_SPIRIT );
  resource_base[ RESOURCE_HEALTH ] = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_HEALTH );
  resource_base[ RESOURCE_MANA   ] = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_MANA );
  base_spell_crit                  = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_SPELL_CRIT );
  base_attack_crit                 = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_MELEE_CRIT );
  initial_spell_crit_per_intellect = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_SPELL_CRIT_PER_INT );
  initial_attack_crit_per_agility  = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_MELEE_CRIT_PER_AGI );
  base_mp5                         = rating_t::get_attribute_base( sim, dbc, level, type, race, BASE_STAT_MP5 );

  if ( level <= 80 ) health_per_stamina = 10;
  else if ( level <= 85 ) health_per_stamina = ( level - 80 ) / 5 * 4 + 10;
  else if ( level <= MAX_LEVEL ) health_per_stamina = 14;
  if ( world_lag_stddev < 0 ) world_lag_stddev = world_lag * 0.1;
  if ( brain_lag_stddev < 0 ) brain_lag_stddev = brain_lag * 0.1;
}

// player_t::init_items =====================================================

void player_t::init_items()
{
  if ( is_pet() ) return;

  std::vector<std::string> splits;
  int num_splits = util_t::string_split( splits, items_str, "/" );
  for ( int i=0; i < num_splits; i++ )
  {
    if ( find_item( splits[ i ] ) )
    {
      sim -> errorf( "Player %s has multiple %s equipped.\n", name(), splits[ i ].c_str() );
    }
    items.push_back( item_t( this, splits[ i ] ) );
  }

  gear_stats_t item_stats;

  bool slots[ SLOT_MAX ];
  for ( int i = 0; i < SLOT_MAX; i++ )
  {
    if ( util_t::armor_type_string( type, i ) )
    {
      slots[ i ] = false;
    }
    else
    {
      slots[ i ] = true;
    }
  }

  int num_items = ( int ) items.size();
  for ( int i=0; i < num_items; i++ )
  {
    // If the item has been specified in options we want to start from scratch, forgetting about lingering stuff from profile copy
    if ( items[ i ].options_str != "" )
    {
      items[ i ] = item_t( this, items[ i ].options_str );
      items[ i ].slot = i;
    }

    item_t& item = items[ i ];

    if ( ! item.init() )
    {
      sim -> errorf( "Unable to initialize item '%s' on player '%s'\n", item.name(), name() );
      sim -> cancel();
      return;
    }

    slots[ item.slot ] = item.matching_type();

    for ( int j=0; j < STAT_MAX; j++ )
    {
      item_stats.add_stat( j, item.stats.get_stat( j ) );
    }
  }

  switch ( type )
  {
  case MAGE:
  case PRIEST:
  case WARLOCK:
    matching_gear = true;
    break;
  default:
    matching_gear = true;
    for ( int i=0; i < SLOT_MAX; i++ )
    {
      if ( slots[ i ] == false )
      {
        matching_gear = false;
        break;
      }
    }
    break;
  }


  init_meta_gem( item_stats );

  for ( int i=0; i < STAT_MAX; i++ )
  {
    if ( gear.get_stat( i ) == 0 )
    {
      gear.set_stat( i, item_stats.get_stat( i ) );
    }
  }

  if ( sim -> debug )
  {
    log_t::output( sim, "%s gear:", name() );
    gear.print( sim -> output_file );
  }

  set_bonus.init( this );
}

// player_t::init_meta_gem ==================================================

void player_t::init_meta_gem( gear_stats_t& item_stats )
{
  if ( ! meta_gem_str.empty() ) meta_gem = util_t::parse_meta_gem_type( meta_gem_str );

  if      ( meta_gem == META_AGILE_SHADOWSPIRIT         ) item_stats.attribute[ ATTR_AGILITY ] += 54;
  else if ( meta_gem == META_AUSTERE_EARTHSIEGE         ) item_stats.attribute[ ATTR_STAMINA ] += 32;
  else if ( meta_gem == META_AUSTERE_SHADOWSPIRIT       ) item_stats.attribute[ ATTR_STAMINA ] += 81;
  else if ( meta_gem == META_BEAMING_EARTHSIEGE         ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_BRACING_EARTHSIEGE         ) item_stats.attribute[ ATTR_INTELLECT ] += 21;
  else if ( meta_gem == META_BRACING_EARTHSTORM         ) item_stats.attribute[ ATTR_INTELLECT ] += 12;
  else if ( meta_gem == META_BRACING_SHADOWSPIRIT       ) item_stats.attribute[ ATTR_INTELLECT ] += 54;
  else if ( meta_gem == META_BURNING_SHADOWSPIRIT       ) item_stats.attribute[ ATTR_INTELLECT ] += 54;
  else if ( meta_gem == META_CHAOTIC_SHADOWSPIRIT       ) item_stats.crit_rating += 54;
  else if ( meta_gem == META_CHAOTIC_SKYFIRE            ) item_stats.crit_rating += 12;
  else if ( meta_gem == META_CHAOTIC_SKYFLARE           ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_DESTRUCTIVE_SHADOWSPIRIT   ) item_stats.crit_rating += 54;
  else if ( meta_gem == META_DESTRUCTIVE_SKYFIRE        ) item_stats.crit_rating += 12;
  else if ( meta_gem == META_DESTRUCTIVE_SKYFLARE       ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_EFFULGENT_SHADOWSPIRIT     ) item_stats.attribute[ ATTR_STAMINA ] += 81;
  else if ( meta_gem == META_EMBER_SHADOWSPIRIT         ) item_stats.attribute[ ATTR_INTELLECT ] += 54;
  else if ( meta_gem == META_EMBER_SKYFIRE              ) item_stats.attribute[ ATTR_INTELLECT ] += 12;
  else if ( meta_gem == META_EMBER_SKYFLARE             ) item_stats.attribute[ ATTR_INTELLECT ] += 21;
  else if ( meta_gem == META_ENIGMATIC_SHADOWSPIRIT     ) item_stats.crit_rating += 54;
  else if ( meta_gem == META_ENIGMATIC_SKYFLARE         ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_ENIGMATIC_STARFLARE        ) item_stats.crit_rating += 17;
  else if ( meta_gem == META_ENIGMATIC_SKYFIRE          ) item_stats.crit_rating += 12;
  else if ( meta_gem == META_ETERNAL_EARTHSIEGE         ) item_stats.dodge_rating += 21;
  else if ( meta_gem == META_ETERNAL_SHADOWSPIRIT       ) item_stats.attribute[ ATTR_STAMINA ] += 81;
  else if ( meta_gem == META_FLEET_SHADOWSPIRIT         ) item_stats.mastery_rating += 54;
  else if ( meta_gem == META_FORLORN_SHADOWSPIRIT       ) item_stats.attribute[ ATTR_INTELLECT ] += 54;
  else if ( meta_gem == META_FORLORN_SKYFLARE           ) item_stats.attribute[ ATTR_INTELLECT ] += 21;
  else if ( meta_gem == META_FORLORN_STARFLARE          ) item_stats.attribute[ ATTR_INTELLECT ] += 17;
  else if ( meta_gem == META_IMPASSIVE_SHADOWSPIRIT     ) item_stats.crit_rating += 54;
  else if ( meta_gem == META_IMPASSIVE_SKYFLARE         ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_IMPASSIVE_STARFLARE        ) item_stats.crit_rating += 17;
  else if ( meta_gem == META_INSIGHTFUL_EARTHSIEGE      ) item_stats.attribute[ ATTR_INTELLECT ] += 21;
  else if ( meta_gem == META_INSIGHTFUL_EARTHSTORM      ) item_stats.attribute[ ATTR_INTELLECT ] += 12;
  else if ( meta_gem == META_INVIGORATING_EARTHSIEGE    ) item_stats.haste_rating += 21;
  else if ( meta_gem == META_PERSISTENT_EARTHSHATTER    ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_PERSISTENT_EARTHSIEGE      ) item_stats.crit_rating += 17;
  else if ( meta_gem == META_POWERFUL_EARTHSHATTER      ) item_stats.attribute[ ATTR_STAMINA ] += 26;
  else if ( meta_gem == META_POWERFUL_EARTHSIEGE        ) item_stats.attribute[ ATTR_STAMINA ] += 32;
  else if ( meta_gem == META_POWERFUL_EARTHSTORM        ) item_stats.attribute[ ATTR_STAMINA ] += 18;
  else if ( meta_gem == META_POWERFUL_SHADOWSPIRIT      ) item_stats.attribute[ ATTR_STAMINA ] += 81;
  else if ( meta_gem == META_RELENTLESS_EARTHSIEGE      ) item_stats.attribute[ ATTR_AGILITY ] += 21;
  else if ( meta_gem == META_RELENTLESS_EARTHSTORM      ) item_stats.attribute[ ATTR_AGILITY ] += 12;
  else if ( meta_gem == META_REVERBERATING_SHADOWSPIRIT ) item_stats.attribute[ ATTR_STRENGTH ] += 54;
  else if ( meta_gem == META_REVITALIZING_SHADOWSPIRIT  ) item_stats.attribute[ ATTR_SPIRIT ] += 54;
  else if ( meta_gem == META_REVITALIZING_SKYFLARE      ) item_stats.attribute[ ATTR_SPIRIT ] += 22;
  else if ( meta_gem == META_SWIFT_SKYFIRE              ) item_stats.crit_rating += 12;
  else if ( meta_gem == META_SWIFT_SKYFLARE             ) item_stats.crit_rating += 21;
  else if ( meta_gem == META_SWIFT_STARFLARE            ) item_stats.crit_rating += 17;
  else if ( meta_gem == META_TIRELESS_STARFLARE         ) item_stats.attribute[ ATTR_INTELLECT ] += 17;
  else if ( meta_gem == META_TIRELESS_SKYFLARE          ) item_stats.attribute[ ATTR_INTELLECT ] += 21;
  else if ( meta_gem == META_TRENCHANT_EARTHSHATTER     ) item_stats.attribute[ ATTR_INTELLECT ] += 17;
  else if ( meta_gem == META_TRENCHANT_EARTHSIEGE       ) item_stats.attribute[ ATTR_INTELLECT ] += 21;

  if ( ( meta_gem == META_AUSTERE_EARTHSIEGE ) || ( meta_gem == META_AUSTERE_SHADOWSPIRIT ) )
  {
    initial_armor_multiplier *= 1.02;
  }
  else if ( ( meta_gem == META_EMBER_SHADOWSPIRIT ) || ( meta_gem == META_EMBER_SKYFIRE ) || ( meta_gem == META_EMBER_SKYFLARE ) )
  {
    mana_per_intellect *= 1.02;
  }
  else if ( meta_gem == META_BEAMING_EARTHSIEGE )
  {
    mana_per_intellect *= 1.02;
  }
  else if ( meta_gem == META_MYSTICAL_SKYFIRE )
  {
    unique_gear_t::register_stat_proc( PROC_SPELL, RESULT_HIT_MASK, "mystical_skyfire", this, STAT_HASTE_RATING, 1, 320, 0.15, 4.0, 45.0 );
  }
  else if ( meta_gem == META_INSIGHTFUL_EARTHSTORM )
  {
    unique_gear_t::register_stat_proc( PROC_SPELL, RESULT_HIT_MASK, "insightful_earthstorm", this, STAT_MANA, 1, 300, 0.05, 0, 15.0 );
  }
  else if ( meta_gem == META_INSIGHTFUL_EARTHSIEGE )
  {
    unique_gear_t::register_stat_proc( PROC_SPELL, RESULT_HIT_MASK, "insightful_earthsiege", this, STAT_MANA, 1, 600, 0.05, 0, 15.0 );
  }
}

// player_t::init_core ======================================================

void player_t::init_core()
{
  initial_stats.  hit_rating = gear.  hit_rating + enchant.  hit_rating + ( is_pet() ? 0 : sim -> enchant.  hit_rating );
  initial_stats. crit_rating = gear. crit_rating + enchant. crit_rating + ( is_pet() ? 0 : sim -> enchant. crit_rating );
  initial_stats.haste_rating = gear.haste_rating + enchant.haste_rating + ( is_pet() ? 0 : sim -> enchant.haste_rating );
  initial_stats.mastery_rating = gear.mastery_rating + enchant.mastery_rating + ( is_pet() ? 0 : sim -> enchant.mastery_rating );

  initial_haste_rating   = initial_stats.haste_rating;
  initial_mastery_rating = initial_stats.mastery_rating;

  for ( int i=0; i < ATTRIBUTE_MAX; i++ )
  {
    initial_stats.attribute[ i ] = gear.attribute[ i ] + enchant.attribute[ i ] + ( is_pet() ? 0 : sim -> enchant.attribute[ i ] );

    attribute[ i ] = attribute_initial[ i ] = attribute_base[ i ] + initial_stats.attribute[ i ];
  }
}

// player_t::init_race ======================================================

void player_t::init_race()
{
  if ( race_str.empty() )
  {
    race_str = util_t::race_type_string( race );
  }
  else
  {
    race = util_t::parse_race_type( race_str );
  }
}

// player_t::init_racials ======================================================

void player_t::init_racials()
{
  if ( race == RACE_GNOME )
  {
    mana_per_intellect *= 1.05;
  }
}

// player_t::init_spell =====================================================

void player_t::init_spell()
{
  initial_stats.spell_power       = gear.spell_power       + enchant.spell_power       + ( is_pet() ? 0 : sim -> enchant.spell_power );
  initial_stats.spell_penetration = gear.spell_penetration + enchant.spell_penetration + ( is_pet() ? 0 : sim -> enchant.spell_penetration );
  initial_stats.mp5               = gear.mp5               + enchant.mp5               + ( is_pet() ? 0 : sim -> enchant.mp5 );

  initial_spell_power[ SCHOOL_MAX ] = base_spell_power + initial_stats.spell_power;

  initial_spell_hit = base_spell_hit + initial_stats.hit_rating / rating.spell_hit;

  initial_spell_crit = base_spell_crit + initial_stats.crit_rating / rating.spell_crit;

  initial_spell_penetration = base_spell_penetration + initial_stats.spell_penetration;

  initial_mp5 = base_mp5 + initial_stats.mp5;

  if ( type != ENEMY && type != ENEMY_ADD )
    mana_regen_base = dbc.regen_spirit( type, level );

  if ( level >= 61 )
  {
    half_resistance_rating = 150.0 + ( level - 60 ) * ( level - 67.5 );
  }
  else if ( level >= 21 )
  {
    half_resistance_rating = 50.0 + ( level - 20 ) * 2.5;
  }
  else
  {
    half_resistance_rating = 50.0;
  }
}

// player_t::init_attack ====================================================

void player_t::init_attack()
{
  initial_stats.attack_power     = gear.attack_power     + enchant.attack_power     + ( is_pet() ? 0 : sim -> enchant.attack_power );
  initial_stats.expertise_rating = gear.expertise_rating + enchant.expertise_rating + ( is_pet() ? 0 : sim -> enchant.expertise_rating );

  initial_attack_power     = base_attack_power     + initial_stats.attack_power;
  initial_attack_hit       = base_attack_hit       + initial_stats.hit_rating       / rating.attack_hit;
  initial_attack_crit      = base_attack_crit      + initial_stats.crit_rating      / rating.attack_crit;
  initial_attack_expertise = base_attack_expertise + initial_stats.expertise_rating / rating.expertise;

  double a,b;
  if ( level > 80 )
  {
    a = 2167.5;
    b = -158167.5;
  }
  else if ( level >= 60 )
  {
    a = 467.5;
    b = -22167.5;
  }
  else
  {
    a = 85.0;
    b = 400.0;
  }
  armor_coeff = a * level + b;
}

// player_t::init_defense ====================================================

void player_t::init_defense()
{
  if ( type != ENEMY && type != ENEMY_ADD )
    base_dodge = dbc.dodge_base( type );

  initial_stats.armor          = gear.armor          + enchant.armor          + ( is_pet() ? 0 : sim -> enchant.armor );
  initial_stats.bonus_armor    = gear.bonus_armor    + enchant.bonus_armor    + ( is_pet() ? 0 : sim -> enchant.bonus_armor );
  initial_stats.dodge_rating   = gear.dodge_rating   + enchant.dodge_rating   + ( is_pet() ? 0 : sim -> enchant.dodge_rating );
  initial_stats.parry_rating   = gear.parry_rating   + enchant.parry_rating   + ( is_pet() ? 0 : sim -> enchant.parry_rating );
  initial_stats.block_rating   = gear.block_rating   + enchant.block_rating   + ( is_pet() ? 0 : sim -> enchant.block_rating );

  initial_armor             = base_armor       + initial_stats.armor;
  initial_bonus_armor       = base_bonus_armor + initial_stats.bonus_armor;
  initial_miss              = base_miss;
  initial_dodge             = base_dodge       + initial_stats.dodge_rating / rating.dodge;
  initial_parry             = base_parry       + initial_stats.parry_rating / rating.parry;
  initial_block             = base_block       + initial_stats.block_rating / rating.block;

  if ( type != ENEMY && type != ENEMY_ADD )
  {
    initial_dodge_per_agility = dbc.dodge_scaling( type, level );
    initial_parry_rating_per_strength = 0.0;
  }

  if ( primary_role() == ROLE_TANK ) position = POSITION_FRONT;
}

// player_t::init_weapon ===================================================

void player_t::init_weapon( weapon_t* w )
{
  if ( w -> type == WEAPON_NONE ) return;

  if ( w -> slot == SLOT_MAIN_HAND ) assert( w -> type >= WEAPON_NONE && w -> type < WEAPON_2H );
  if ( w -> slot == SLOT_OFF_HAND  ) assert( w -> type >= WEAPON_NONE && w -> type < WEAPON_2H );
  if ( w -> slot == SLOT_RANGED    ) assert( w -> type == WEAPON_NONE || ( w -> type > WEAPON_2H && w -> type < WEAPON_RANGED ) );
}

// player_t::init_unique_gear ==============================================

void player_t::init_unique_gear()
{
  unique_gear_t::init( this );
}

// player_t::init_enchant ==================================================

void player_t::init_enchant()
{
  enchant_t::init( this );
}

// player_t::init_resources ================================================

void player_t::init_resources( bool force )
{
  // The first 20pts of intellect/stamina only provide 1pt of mana/health.
  // Code simplified on the assumption that the minimum player level is 60.
  double adjust = ( is_pet() || is_enemy() || is_add() ) ? 0 : 20;

  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    if ( force || resource_initial[ i ] == 0 )
    {
      resource_initial[ i ] = resource_base[ i ] + gear.resource[ i ] + enchant.resource[ i ] + ( is_pet() ? 0 : sim -> enchant.resource[ i ] );

      if ( i == RESOURCE_MANA )
      {
        if ( ( meta_gem == META_EMBER_SHADOWSPIRIT ) || ( meta_gem == META_EMBER_SKYFIRE ) || ( meta_gem == META_EMBER_SKYFLARE ) )
        {
          resource_initial[ i ] *= 1.02;
        }
        if ( race == RACE_GNOME )
        {
          resource_initial[ i ] *= 1.05;
        }
        resource_initial[ i ] += ( floor( intellect() ) - adjust ) * mana_per_intellect + adjust;
      }
      if ( i == RESOURCE_HEALTH )
      {
        resource_initial[ i ] += ( floor( stamina() ) - adjust ) * health_per_stamina + adjust;

        if ( buffs.hellscreams_warsong -> check() || buffs.strength_of_wrynn -> check() )
        {
          // ICC buff.
          resource_initial[ i ] *= 1.30;
        }
      }
    }
    resource_current[ i ] = resource_max[ i ] = resource_initial[ i ];
  }

  if ( timeline_resource.empty() )
  {
    int size = ( int ) sim -> max_time;
    if ( size == 0 ) size = 600; // Default to 10 minutes
    size *= 2;
    timeline_resource.insert( timeline_resource.begin(), size, 0 );
  }
  if ( timeline_health.empty() )
  {
    int size = ( int ) sim -> max_time;
    if ( size == 0 ) size = 600; // Default to 10 minutes
    size *= 2;
    timeline_health.insert( timeline_health.begin(), size, 0 );
  }
}

// player_t::init_consumables ==============================================

void player_t::init_consumables()
{
  consumable_t::init_flask  ( this );
  consumable_t::init_elixirs( this );
  consumable_t::init_food   ( this );
}

// player_t::init_professions ==============================================

void player_t::init_professions()
{
  if ( professions_str.empty() ) return;

  std::vector<std::string> splits;
  int size = util_t::string_split( splits, professions_str, ",/" );

  for ( int i=0; i < size; i++ )
  {
    std::string prof_name;
    int prof_value=0;

    if ( 2 != util_t::string_split( splits[ i ], "=", "S i", &prof_name, &prof_value ) )
    {
      prof_name  = splits[ i ];
      prof_value = 525;
    }

    int prof_type = util_t::parse_profession_type( prof_name );
    if ( prof_type == PROFESSION_NONE )
    {
      sim -> errorf( "Invalid profession encoding: %s\n", professions_str.c_str() );
      return;
    }

    profession[ prof_type ] = prof_value;
  }

  // Miners gain additional stamina
  if      ( profession[ PROF_MINING ] >= 525 ) attribute_initial[ ATTR_STAMINA ] += 120.0;
  else if ( profession[ PROF_MINING ] >= 450 ) attribute_initial[ ATTR_STAMINA ] +=  60.0;
  else if ( profession[ PROF_MINING ] >= 375 ) attribute_initial[ ATTR_STAMINA ] +=  30.0;
  else if ( profession[ PROF_MINING ] >= 300 ) attribute_initial[ ATTR_STAMINA ] +=  10.0;
  else if ( profession[ PROF_MINING ] >= 225 ) attribute_initial[ ATTR_STAMINA ] +=   7.0;
  else if ( profession[ PROF_MINING ] >= 150 ) attribute_initial[ ATTR_STAMINA ] +=   5.0;
  else if ( profession[ PROF_MINING ] >=  75 ) attribute_initial[ ATTR_STAMINA ] +=   3.0;

  // Skinners gain additional crit rating
  if      ( profession[ PROF_SKINNING ] >= 525 ) initial_attack_crit += 80.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 450 ) initial_attack_crit += 40.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 375 ) initial_attack_crit += 20.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 300 ) initial_attack_crit += 12.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 225 ) initial_attack_crit +=  9.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >= 150 ) initial_attack_crit +=  6.0 / rating.attack_crit;
  else if ( profession[ PROF_SKINNING ] >=  75 ) initial_attack_crit +=  3.0 / rating.attack_crit;
}

// Execute Pet Action =========================================================

struct execute_pet_action_t : public action_t
{
  action_t* pet_action;
  pet_t* pet;
  std::string action_str;

  execute_pet_action_t( player_t* player, pet_t* p, const std::string& as, const std::string& options_str ) :
    action_t( ACTION_OTHER, "execute_pet_action", player )
  {
    parse_options( NULL, options_str );
    pet = p;
    action_str = as;
    trigger_gcd = 0;
  }

  virtual void reset()
  {
    for ( action_t* action = pet -> action_list; action; action = action -> next )
    {
      if ( action -> name_str == action_str )
      {
        action -> background = true;
        pet_action = action;
      }
    }
  }

  virtual void execute()
  {
    pet_action -> execute();
  }

  virtual bool ready()
  {
    if ( ! action_t::ready() ) return false;
    if ( pet_action -> player -> sleeping ) return false;
    return pet_action -> ready();
  }
};

// player_t::init_target ============================================================

void player_t::init_target()
{
  if ( ! target_str.empty() )
  {
      target = sim -> find_player( target_str );
  }
  if ( ! target )
  {
    target = sim -> target;
  }
}

// player_t::init_use_item_actions ==================================================

void player_t::init_use_item_actions( const std::string& append )
{
    int num_items = ( int ) items.size();
    for ( int i=0; i < num_items; i++ )
    {
      if ( items[ i ].use.active() )
      {
        action_list_str += "/use_item,name=";
        action_list_str += items[ i ].name();
        if ( ! append.empty() )
        {
          action_list_str += append;
        }
      }
    }
}

// player_t::init_use_profession_actions ==================================================

void player_t::init_use_profession_actions( const std::string& append )
{
    // Lifeblood
    if ( profession[ PROF_HERBALISM ] >= 450 )
    {
      action_list_str += "/lifeblood";
    }
    else
    {
      return;
    }
    if ( ! append.empty() )
    {
      action_list_str += append;
    }
}



// player_t::init_use_racial_actions ==================================================

void player_t::init_use_racial_actions( const std::string& append )
{
    if ( race == RACE_ORC )
    {
      action_list_str += "/blood_fury";
    }
    else if ( race == RACE_TROLL )
    {
      action_list_str += "/berserking";
    }
    else if ( race == RACE_BLOOD_ELF )
    {
      action_list_str += "/arcane_torrent";
    }
    else
    {
      return;
    }
    if ( ! append.empty() )
    {
      action_list_str += append;
    }
}


// player_t::init_actions ==================================================

void player_t::init_actions()
{

  if ( ! action_list_str.empty() )
  {
    if ( action_list_default && sim -> debug ) log_t::output( sim, "Player %s using default actions", name() );

    if ( sim -> debug ) log_t::output( sim, "Player %s: action_list_str=%s", name(), action_list_str.c_str() );

    std::string modify_action_options = "";

    if ( ! modify_action.empty() )
    {
      std::string::size_type cut_pt = modify_action.find( "," );

      if ( cut_pt != modify_action.npos )
      {
        modify_action_options = modify_action.substr( cut_pt + 1 );
        modify_action         = modify_action.substr( 0, cut_pt );
      }
    }

    std::vector<std::string> splits;
    int num_splits = util_t::string_split( splits, action_list_str, "/" );

    for ( int i=0; i < num_splits; i++ )
    {
      std::string action_name    = splits[ i ];
      std::string action_options = "";

      std::string::size_type cut_pt = action_name.find( "," );

      if ( cut_pt != action_name.npos )
      {
        action_options = action_name.substr( cut_pt + 1 );
        action_name    = action_name.substr( 0, cut_pt );
      }

      action_t* a=0;

      cut_pt = action_name.find( ":" );
      if ( cut_pt != action_name.npos )
      {
        std::string pet_name   = action_name.substr( 0, cut_pt );
        std::string pet_action = action_name.substr( cut_pt + 1 );

        pet_t* pet = find_pet( pet_name );
        if ( pet )
        {
          a =  new execute_pet_action_t( this, pet, pet_action, action_options );
        }
        else
        {
          sim -> errorf( "Player %s refers to unknown pet %s in action: %s\n",
                         name(), pet_name.c_str(), splits[ i ].c_str() );
        }
      }
      else
      {
        if ( action_name == modify_action )
        {
          if ( sim -> debug )
            log_t::output( sim, "Player %s: modify_action=%s", name(), modify_action.c_str() );

          action_options = modify_action_options;
          splits[ i ] = modify_action + "," + modify_action_options;
        }
        a = create_action( action_name, action_options );
      }

      if ( a )
      {
        a -> marker = ( char ) ( ( i < 10 ) ? ( '0' + i      ) :
                                 ( i < 36 ) ? ( 'A' + i - 10 ) :
                                 ( i < 58 ) ? ( 'a' + i - 36 ) : '.' );

        a -> signature_str = splits[ i ];
      }
      else
      {
        sim -> errorf( "Player %s unable to create action: %s\n", name(), splits[ i ].c_str() );
        sim -> cancel();
        return;
      }
    }
  }

  if ( ! action_list_skip.empty() )
  {
    if ( sim -> debug )
      log_t::output( sim, "Player %s: action_list_skip=%s", name(), action_list_skip.c_str() );

    std::vector<std::string> splits;
    int num_splits = util_t::string_split( splits, action_list_skip, "/" );
    for ( int i=0; i < num_splits; i++ )
    {
      action_t* action = find_action( splits[ i ] );
      if ( action ) action -> background = true;
    }
  }

  for ( action_t* action = action_list; action; action = action -> next )
  {
    action -> init();
  }

  int capacity = std::max( 1200, ( int ) ( sim -> max_time / 2.0 ) );
  action_sequence.reserve( capacity );
  action_sequence = "";
}

// player_t::init_rating ===================================================

void player_t::init_rating()
{
  if ( sim -> debug )
    log_t::output( sim, "player_t::init_rating(): level=%d type=%s",
                   level, util_t::player_type_string( type ) );

  rating.init( sim, dbc, level, type );
}

// player_t::init_talents =================================================

void player_t::init_talents()
{
  for( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    talent_tab_points[ i ] = 0;

    for( int j=talent_trees[ i ].size()-1; j >= 0; j-- )
    {
      talent_tab_points[ i ] += talent_trees[ i ][ j ] -> rank();
    }
  }

  specialization = primary_tab();
}

// player_t::init_glyphs ==================================================

void player_t::init_glyphs()
{
  std::vector<std::string> glyph_names;
  int num_glyphs = util_t::string_split( glyph_names, glyphs_str, ",/" );

  for ( int i=0; i < num_glyphs; i++ )
  {
    glyph_t* g = find_glyph( glyph_names[ i ] );

    if ( g ) g -> enable();
  }
}

// player_t::init_spells =================================================

void player_t::init_spells()
{

}

// player_t::init_buffs ====================================================

void player_t::init_buffs()
{
  buffs.berserking                = new buff_t( this, 26297, "berserking"                   );
  buffs.corruption_absolute       = new buff_t( this, 82170, "corruption_absolute"          );
  buffs.essence_of_the_red        = new buff_t( this,        "essence_of_the_red"           );
  buffs.heroic_presence           = new buff_t( this,        "heroic_presence",     1       );
  buffs.replenishment             = new buff_t( this, 57669, "replenishment"                );
  buffs.stoneform                 = new buff_t( this, 65116, "stoneform"                    );
  buffs.hellscreams_warsong       = new buff_t( this,        "hellscreams_warsong", 1       );
  buffs.strength_of_wrynn         = new buff_t( this,        "strength_of_wrynn",   1       );
  buffs.dark_intent               = new buff_t( this, 85767, "dark_intent"                  );
  buffs.dark_intent_feedback      = new buff_t( this, 85759, "dark_intent_feedback"         );
  buffs.furious_howl              = new buff_t( this, 24604, "furious_howl"                 );
  buffs.hymn_of_hope = new hymn_of_hope_buff_t( this, 64904, "hymn_of_hope"                 );
  buffs.body_and_soul             = new buff_t( this,        "body_and_soul",       1,  4.0 );
  buffs.grace                     = new buff_t( this,        "grace",               3, 15.0 );
  buffs.blessing_of_ancient_kings = new buff_t( this, 64411, "blessing_of_ancient_kings"    );
  buffs.blessing_of_ancient_kings -> buff_duration = 15.0; // FIXME: remove once buff is in dbc

  buffs.raid_movement = new buff_t( this, "raid_movement", 1 );
  buffs.self_movement = new buff_t( this, "self_movement", 1 );

  // stat_buff_t( sim, name, stat, amount, max_stack, duration, cooldown, proc_chance, quiet )
  buffs.blood_fury_ap          = new stat_buff_t( this, "blood_fury_ap",          STAT_ATTACK_POWER, is_enemy() ? 0 : floor( sim -> dbc.effect_average( sim -> dbc.spell( 33697 ) -> effect1().id(), sim -> max_player_level ) ), 1, 15.0 );
  buffs.blood_fury_sp          = new stat_buff_t( this, "blood_fury_sp",          STAT_SPELL_POWER,  is_enemy() ? 0 : floor( sim -> dbc.effect_average( sim -> dbc.spell( 33697 ) -> effect2().id(), sim -> max_player_level ) ), 1, 15.0 );

  buffs.destruction_potion     = new stat_buff_t( this, "destruction_potion",     STAT_SPELL_POWER,   120.0,            1, 15.0, 60.0 );
  buffs.indestructible_potion  = new stat_buff_t( this, "indestructible_potion",  STAT_ARMOR,        3500.0,            1, 15.0, 60.0 );
  buffs.lifeblood              = new stat_buff_t( this, "lifeblood",              STAT_HASTE_RATING,  480.0,            1, 20.0       );
  buffs.speed_potion           = new stat_buff_t( this, "speed_potion",           STAT_HASTE_RATING,  500.0,            1, 15.0, 60.0 );
  buffs.earthen_potion         = new stat_buff_t( this, "earthen_potion",         STAT_ARMOR,        4800.0,            1, 25.0, 60.0 );
  buffs.golemblood_potion      = new stat_buff_t( this, "golemblood_potion",      STAT_STRENGTH,     1200.0,            1, 25.0, 60.0 );
  buffs.tolvir_potion          = new stat_buff_t( this, "tolvir_potion",          STAT_AGILITY,      1200.0,            1, 25.0, 60.0 );
  buffs.volcanic_potion        = new stat_buff_t( this, "volcanic_potion",        STAT_INTELLECT,    1200.0,            1, 25.0, 60.0 );
  buffs.wild_magic_potion_sp   = new stat_buff_t( this, "wild_magic_potion_sp",   STAT_SPELL_POWER,   200.0,            1, 15.0, 60.0 );
  buffs.wild_magic_potion_crit = new stat_buff_t( this, "wild_magic_potion_crit", STAT_CRIT_RATING,   200.0,            1, 15.0, 60.0 );

  // Infinite-Stacking Buffs and De-Buffs

  buffs.stunned        = new   buff_t( this, "stunned",      -1 );
  debuffs.bleeding     = new debuff_t( this, "bleeding",     -1 );
  debuffs.casting      = new debuff_t( this, "casting",      -1 );
  debuffs.invulnerable = new debuff_t( this, "invulnerable", -1 );
  debuffs.vulnerable   = new debuff_t( this, "vulnerable",   -1 );
}

// player_t::init_gains ====================================================

void player_t::init_gains()
{
  gains.arcane_torrent         = get_gain( "arcane_torrent" );
  gains.blessing_of_might      = get_gain( "blessing_of_might" );
  gains.dark_rune              = get_gain( "dark_rune" );
  gains.energy_regen           = get_gain( "energy_regen" );
  gains.essence_of_the_red     = get_gain( "essence_of_the_red" );
  gains.focus_regen            = get_gain( "focus_regen" );
  gains.innervate              = get_gain( "innervate" );
  gains.mana_potion            = get_gain( "mana_potion" );
  gains.mana_spring_totem      = get_gain( "mana_spring_totem" );
  gains.mp5_regen              = get_gain( "mp5_regen" );
  gains.replenishment          = get_gain( "replenishment" );
  gains.restore_mana           = get_gain( "restore_mana" );
  gains.spellsurge             = get_gain( "spellsurge" );
  gains.spirit_intellect_regen = get_gain( "spirit_intellect_regen" );
  gains.vampiric_embrace       = get_gain( "vampiric_embrace" );
  gains.vampiric_touch         = get_gain( "vampiric_touch" );
  gains.water_elemental        = get_gain( "water_elemental" );
  gains.hymn_of_hope           = get_gain( "hymn_of_hope_max_mana" );
}

// player_t::init_procs ====================================================

void player_t::init_procs()
{
  procs.hat_donor = get_proc( "hat_donor" );
}

// player_t::init_uptimes ==================================================

void player_t::init_uptimes()
{
}

// player_t::init_rng ======================================================

void player_t::init_rng()
{
  rngs.lag_channel  = get_rng( "lag_channel"  );
  rngs.lag_gcd      = get_rng( "lag_gcd"      );
  rngs.lag_queue    = get_rng( "lag_queue"    );
  rngs.lag_ability  = get_rng( "lag_ability"  );
  rngs.lag_reaction = get_rng( "lag_reaction" );
  rngs.lag_world    = get_rng( "lag_world"    );
  rngs.lag_brain    = get_rng( "lag_brain"    );
}

// player_t::init_stats ====================================================

void player_t::init_stats()
{
  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    resource_lost[ i ] = resource_gained[ i ] = 0;
  }

  iteration_dps.clear();
  iteration_dps.insert( iteration_dps.begin(), sim -> iterations, 0 );
}

// player_t::init_values ====================================================

void player_t::init_values()
{

}

// player_t::init_scaling ==================================================

void player_t::init_scaling()
{
  if ( ! is_pet() && ! is_enemy() )
  {
    invert_scaling = 0;

    int role = primary_role();

    int attack = ( ( role == ROLE_ATTACK ) || ( role == ROLE_HYBRID ) ) ? 1 : 0;
    int spell  = ( ( role == ROLE_SPELL  ) || ( role == ROLE_HYBRID ) || ( role == ROLE_HEAL ) ) ? 1 : 0;
    int tank   = role == ROLE_TANK ? 1 : 0;

    scales_with[ STAT_STRENGTH  ] = attack;
    scales_with[ STAT_AGILITY   ] = attack || tank;
    scales_with[ STAT_STAMINA   ] = tank;
    scales_with[ STAT_INTELLECT ] = spell;
    scales_with[ STAT_SPIRIT    ] = spell;

    scales_with[ STAT_HEALTH ] = 0;
    scales_with[ STAT_MANA   ] = 0;
    scales_with[ STAT_RAGE   ] = 0;
    scales_with[ STAT_ENERGY ] = 0;
    scales_with[ STAT_FOCUS  ] = 0;
    scales_with[ STAT_RUNIC  ] = 0;

    scales_with[ STAT_SPELL_POWER       ] = spell;
    scales_with[ STAT_SPELL_PENETRATION ] = 0;
    scales_with[ STAT_MP5               ] = 0;

    scales_with[ STAT_ATTACK_POWER             ] = attack;
    scales_with[ STAT_EXPERTISE_RATING         ] = attack || tank;

    scales_with[ STAT_HIT_RATING                ] = 1;
    scales_with[ STAT_CRIT_RATING               ] = 1;
    scales_with[ STAT_HASTE_RATING              ] = 1;
    scales_with[ STAT_MASTERY_RATING            ] = 1;

    scales_with[ STAT_WEAPON_DPS   ] = attack;
    scales_with[ STAT_WEAPON_SPEED ] = sim -> weapon_speed_scale_factors ? attack : 0;

    scales_with[ STAT_WEAPON_OFFHAND_DPS   ] = 0;
    scales_with[ STAT_WEAPON_OFFHAND_SPEED ] = 0;

    scales_with[ STAT_ARMOR          ] = tank;
    scales_with[ STAT_BONUS_ARMOR    ] = 0;
    scales_with[ STAT_DODGE_RATING   ] = tank;
    scales_with[ STAT_PARRY_RATING   ] = 0;

    scales_with[ STAT_BLOCK_RATING ] = 0;

    if ( sim -> scaling -> scale_stat != STAT_NONE )
    {
      double v = sim -> scaling -> scale_value;

      switch ( sim -> scaling -> scale_stat )
      {
      case STAT_STRENGTH:  attribute_initial[ ATTR_STRENGTH  ] += v; break;
      case STAT_AGILITY:   attribute_initial[ ATTR_AGILITY   ] += v; break;
      case STAT_STAMINA:   attribute_initial[ ATTR_STAMINA   ] += v; break;
      case STAT_INTELLECT: attribute_initial[ ATTR_INTELLECT ] += v; break;
      case STAT_SPIRIT:    attribute_initial[ ATTR_SPIRIT    ] += v; break;

      case STAT_SPELL_POWER:       initial_spell_power[ SCHOOL_MAX ] += v; break;
      case STAT_SPELL_PENETRATION: initial_spell_penetration         += v; break;
      case STAT_MP5:               initial_mp5                       += v; break;

      case STAT_ATTACK_POWER:             initial_attack_power       += v;                            break;
      case STAT_EXPERTISE_RATING:         initial_attack_expertise   += v / rating.expertise;         break;

      case STAT_HIT_RATING:
        initial_attack_hit += v / rating.attack_hit;
        initial_spell_hit  += v / rating.spell_hit;
        break;

      case STAT_CRIT_RATING:
        initial_attack_crit += v / rating.attack_crit;
        initial_spell_crit  += v / rating.spell_crit;
        break;

      case STAT_HASTE_RATING: initial_haste_rating += v; break;
      case STAT_MASTERY_RATING: initial_mastery_rating += v; break;

      case STAT_WEAPON_DPS:
        if ( main_hand_weapon.damage > 0 )
        {
          main_hand_weapon.damage  += main_hand_weapon.swing_time * v;
          main_hand_weapon.min_dmg += main_hand_weapon.swing_time * v;
          main_hand_weapon.max_dmg += main_hand_weapon.swing_time * v;
        }
        if ( ranged_weapon.damage > 0 )
        {
          ranged_weapon.damage     += ranged_weapon.swing_time * v;
          ranged_weapon.min_dmg    += ranged_weapon.swing_time * v;
          ranged_weapon.max_dmg    += ranged_weapon.swing_time * v;
        }
        break;

      case STAT_WEAPON_SPEED:
        if ( main_hand_weapon.swing_time > 0 )
        {
          double new_speed = ( main_hand_weapon.swing_time + v );
          double mult = new_speed / main_hand_weapon.swing_time;

          main_hand_weapon.min_dmg *= mult;
          main_hand_weapon.max_dmg *= mult;
          main_hand_weapon.damage  *= mult;

          main_hand_weapon.swing_time = new_speed;
        }
        if ( ranged_weapon.swing_time > 0 )
        {
          double new_speed = ( ranged_weapon.swing_time + v );

          double mult = new_speed / ranged_weapon.swing_time;

          ranged_weapon.min_dmg *= mult;
          ranged_weapon.max_dmg *= mult;
          ranged_weapon.damage  *= mult;

          ranged_weapon.swing_time = new_speed;
        }
        break;

      case STAT_WEAPON_OFFHAND_DPS:
        if ( off_hand_weapon.damage > 0 )
        {
          off_hand_weapon.damage   += off_hand_weapon.swing_time * v;
          off_hand_weapon.min_dmg  += off_hand_weapon.swing_time * v;
          off_hand_weapon.max_dmg  += off_hand_weapon.swing_time * v;
        }
        break;

      case STAT_WEAPON_OFFHAND_SPEED:
        if ( off_hand_weapon.swing_time > 0 )
        {
          double new_speed = ( off_hand_weapon.swing_time + v );
          double mult = new_speed / off_hand_weapon.swing_time;

          off_hand_weapon.min_dmg *= mult;
          off_hand_weapon.max_dmg *= mult;
          off_hand_weapon.damage  *= mult;

          off_hand_weapon.swing_time = new_speed;
        }
        break;

      case STAT_ARMOR:          initial_armor       += v; break;
      case STAT_BONUS_ARMOR:    initial_bonus_armor += v; break;
      case STAT_DODGE_RATING:   initial_dodge       += v / rating.dodge; break;
      case STAT_PARRY_RATING:   initial_parry       += v / rating.parry; break;

      case STAT_BLOCK_RATING: initial_block       += v / rating.block; break;


      case STAT_MAX: break;

      default: assert( 0 );
      }
    }
  }
}

// player_t::find_item ======================================================

item_t* player_t::find_item( const std::string& str )
{
  int num_items = ( int ) items.size();

  for ( int i=0; i < num_items; i++ )
    if ( str == items[ i ].name() )
      return &( items[ i ] );

  return 0;
}

// player_t::energy_regen_per_second ======================================

double player_t::energy_regen_per_second() SC_CONST
{
  double r = base_energy_regen_per_second * ( 1.0 / composite_attack_haste() );

  return r;
}

// player_t::focus_regen_per_second ======================================

double player_t::focus_regen_per_second() SC_CONST
{
  double r = base_focus_regen_per_second * ( 1.0 / composite_attack_haste() );

  return r;
}

// player_t::composite_attack_haste ========================================

double player_t::composite_attack_haste() SC_CONST
{
  double h = attack_haste;

  if ( type != PLAYER_GUARDIAN )
  {
    if ( buffs.bloodlust -> up() )
    {
      h *= 1.0 / ( 1.0 + 0.30 );
    }

    if ( buffs.essence_of_the_red -> up() )
    {
      h *= 1.0 / ( 1.0 + 1.0 );
    }

    if ( buffs.unholy_frenzy -> up() )
    {
      h *= 1.0 / ( 1.0 + 0.20 );
    }

    if ( type != PLAYER_PET )
    {
      if ( buffs.mongoose_mh && buffs.mongoose_mh -> up() ) h *= 1.0 / ( 1.0 + 30 / rating.attack_haste );
      if ( buffs.mongoose_oh && buffs.mongoose_oh -> up() ) h *= 1.0 / ( 1.0 + 30 / rating.attack_haste );
    }
    if ( ( race == RACE_GOBLIN ) )
    {
      h *= 1.0 / ( 1.0 + 0.01 );
    }
  }

  return h;
}

// player_t::composite_attack_speed ========================================

double player_t::composite_attack_speed() SC_CONST
{
  double h = composite_attack_haste();

  if ( type != PLAYER_GUARDIAN )
  {
    if ( buffs.berserking -> up() )
    {
      h *= 1.0 / ( 1.0 + 0.20 );
    }
  }

  if ( ! is_enemy() && ! is_add() )
    h *= 1.0 / ( 1.0 + std::max( sim -> auras.hunting_party       -> value(),
                       std::max( sim -> auras.windfury_totem      -> value(),
                                 sim -> auras.improved_icy_talons -> value() ) ) );

  return h;
}

// player_t::composite_attack_power ========================================

double player_t::composite_attack_power() SC_CONST
{
  double ap = attack_power;

  ap += attack_power_per_strength * ( strength() - 10 );
  ap += attack_power_per_agility  * ( agility() - 10 );

  if ( vengeance_enabled )
    ap += vengeance_value;

  return ap;
}

// player_t::composite_attack_crit =========================================

double player_t::composite_attack_crit() SC_CONST
{
  double ac = attack_crit + attack_crit_per_agility * agility();

  if ( ! is_pet() && ! is_enemy() && ! is_add() )
  {
    if ( sim -> auras.leader_of_the_pack -> up()  ||
         sim -> auras.honor_among_thieves -> up() ||
         sim -> auras.elemental_oath -> up()      ||
         sim -> auras.rampage -> up()             ||
         buffs.furious_howl -> up() )
    {
      ac += 0.05;
    }
  }

  if ( race == RACE_WORGEN )
  {
    ac += 0.01;
  }

  return ac;
}

// player_t::composite_attack_hit ==========================================

double player_t::composite_attack_hit() SC_CONST
{
  double ah = attack_hit;

  // Changes here may need to be reflected in the corresponding pet_t
  // function in simulationcraft.h
  if ( buffs.heroic_presence -> up() ) ah += 0.01;

  return ah;
}

// player_t::composite_armor =========================================

double player_t::composite_armor() SC_CONST
{
  double a = armor;

  a *= composite_armor_multiplier();

  a += bonus_armor;

  if ( sim -> auras.devotion_aura -> check() && ! is_enemy() && ! is_add() )
    a += sim -> auras.devotion_aura -> value();

  a *= 1.0 - std::max( debuffs.sunder_armor -> stack() * 0.04,
             std::max( debuffs.faerie_fire  -> check() * debuffs.faerie_fire -> value(),
             std::max( debuffs.expose_armor -> value(),
             std::max( debuffs.corrosive_spit -> check() * debuffs.corrosive_spit -> value() * 0.01,
                       debuffs.tear_armor -> check() * debuffs.tear_armor -> value() * 0.01 ) ) ) )
             - debuffs.shattering_throw -> stack() * 0.20;

  if ( buffs.stoneform -> up() )
    a *= 1.10;

  return a;
}

double player_t::composite_armor_multiplier() SC_CONST
{
  double a = armor_multiplier;

  return a;
}

double player_t::composite_spell_resistance( const school_type school ) SC_CONST
{
  double a = spell_resistance[ school ];

  return a;
}

// player_t::composite_tank_miss ===========================================

double player_t::composite_tank_miss( const school_type school ) SC_CONST
{
  double m = 0;

  if ( school == SCHOOL_PHYSICAL )
  {
    if ( race == RACE_NIGHT_ELF ) // Quickness
    {
      m += 0.02;
    }
  }

  if      ( m > 1.0 ) m = 1.0;
  else if ( m < 0.0 ) m = 0.0;

  return m;
}

// player_t::composite_tank_dodge ====================================

double player_t::composite_tank_dodge() SC_CONST
{
  double d = dodge;

  d += agility() * dodge_per_agility;

  return d;
}

// player_t::composite_tank_parry ====================================

double player_t::composite_tank_parry() SC_CONST
{
  double p = parry;

  p += strength() * parry_rating_per_strength / rating.parry;

  return p;
}

// player_t::composite_tank_block ===================================

double player_t::composite_tank_block() SC_CONST
{
  double b = block;

  return b;
}

// player_t::composite_tank_crit_block ===================================

double player_t::composite_tank_crit_block() SC_CONST
{
  return 0;
}

// player_t::composite_tank_crit ==========================================

double player_t::composite_tank_crit( const school_type school ) SC_CONST
{
  return 0;
}

// player_t::diminished_dodge ========================================

double player_t::diminished_dodge() SC_CONST
{
  if ( diminished_kfactor == 0 || diminished_dodge_capi == 0 ) return 0;

  // Only contributions from gear are subject to diminishing returns;

  double d = stats.dodge_rating / rating.dodge;

  d += dodge_per_agility * stats.attribute[ ATTR_AGILITY ] * composite_attribute_multiplier( ATTR_AGILITY );

  if ( d == 0 ) return 0;

  double diminished_d = 0.01 / ( diminished_dodge_capi + diminished_kfactor / d );

  double loss = d - diminished_d;

  return loss > 0 ? loss : 0;
}

// player_t::diminished_parry ========================================

double player_t::diminished_parry() SC_CONST
{
  if ( diminished_kfactor == 0 || diminished_parry_capi == 0 ) return 0;

  // Only contributions from gear are subject to diminishing returns;

  double p = stats.parry_rating / rating.parry;

  p += parry_rating_per_strength * stats.attribute[ ATTR_STRENGTH ] * composite_attribute_multiplier( ATTR_STRENGTH ) / rating.parry;

  if ( p == 0 ) return 0;

  double diminished_p = 0.01 / ( diminished_parry_capi + diminished_kfactor / p );

  double loss = p - diminished_p;

  return loss > 0 ? loss : 0;
}

// player_t::composite_spell_haste ========================================

double player_t::composite_spell_haste() SC_CONST
{
  double h = spell_haste;

  if ( type != PLAYER_GUARDIAN && ! is_enemy() && ! is_add() )
  {
    if ( buffs.dark_intent -> up() )
      h *= 1.0 / 1.03;

    if ( buffs.bloodlust -> up() )
    {
      h *= 1.0 / ( 1.0 + 0.30 );
    }
    else if ( buffs.power_infusion -> up() )
    {
      h *= 1.0 / ( 1.0 + 0.20 );
    }

    if ( buffs.essence_of_the_red -> up() )
      h *= 1.0 / ( 1.0 + 1.0 );

    if ( buffs.berserking -> up() )
      h *= 1.0 / ( 1.0 + 0.20 );

    if ( ! is_pet() && ! is_enemy() && ! is_add() )
    {
      if ( sim -> auras.wrath_of_air -> up() || sim -> auras.moonkin -> up() || sim -> auras.mind_quickening -> up() )
      {
        h *= 1.0 / ( 1.0 + 0.05 );
      }
    }

    if ( race == RACE_GOBLIN )
    {
      h *= 1.0 / ( 1.0 + 0.01 );
    }
  }

  return h;
}

// player_t::composite_spell_power ========================================

double player_t::composite_spell_power( const school_type school ) SC_CONST
{
  double sp = spell_power[ school ];

  if ( school == SCHOOL_FROSTFIRE )
  {
    sp = std::max( spell_power[ SCHOOL_FROST ],
                   spell_power[ SCHOOL_FIRE  ] );
  }
  else if ( school == SCHOOL_SPELLSTORM )
  {
    sp = std::max( spell_power[ SCHOOL_NATURE ],
                   spell_power[ SCHOOL_ARCANE ] );
  }
  else if ( school == SCHOOL_SHADOWFROST )
  {
    sp = std::max( spell_power[ SCHOOL_SHADOW ],
                   spell_power[ SCHOOL_FROST ] );
  }
  else if ( school == SCHOOL_SHADOWFLAME )
  {
    sp = std::max( spell_power[ SCHOOL_SHADOW ],
                   spell_power[ SCHOOL_FIRE ] );
  }
  if ( school != SCHOOL_MAX ) sp += spell_power[ SCHOOL_MAX ];

  sp += spell_power_per_intellect * ( intellect() - 10 ); // The spellpower is always lower by 10, cata beta build 12803
  sp += spell_power_per_spirit    * spirit();

  return sp;
}

// player_t::composite_spell_power_multiplier =============================

double player_t::composite_spell_power_multiplier() SC_CONST
{
  double m = spell_power_multiplier;
  if ( type != PLAYER_GUARDIAN && ! is_enemy() && ! is_add() )
  {
    if ( sim -> auras.demonic_pact -> up() )
    {
      m *= 1.10;
    }
    else
    {
      m *= 1.0 + std::max( sim -> auras.flametongue_totem -> value(),
                           buffs.arcane_brilliance -> up() * 0.06 );
    }
  }
  return m;
}

// player_t::composite_spell_crit ==========================================

double player_t::composite_spell_crit() SC_CONST
{
  double sc = spell_crit + spell_crit_per_intellect * intellect();

  if ( ! is_pet() && ! is_enemy() && ! is_add() )
  {
    if ( buffs.focus_magic -> up() ) sc += 0.03;

    if ( sim -> auras.leader_of_the_pack -> up() ||
         sim -> auras.honor_among_thieves -> up() ||
         sim -> auras.elemental_oath -> up() ||
         sim -> auras.rampage -> up() ||
         buffs.furious_howl -> up() )
    {
      sc += 0.05;
    }

    if ( buffs.destruction_potion -> check() ) sc += 0.02;
  }

  if ( ( race == RACE_WORGEN ) )
  {
    sc += 0.01;
  }

  return sc;
}

// player_t::composite_spell_hit ===========================================

double player_t::composite_spell_hit() SC_CONST
{
  double sh = spell_hit;

  // Changes here may need to be reflected in the corresponding pet_t
  // function in simulationcraft.h
  if ( buffs.heroic_presence -> up() ) sh += 0.01;

  return sh;
}

// player_t::composite_mp5 =================================================

double player_t::composite_mp5() SC_CONST
{
  return mp5 + mp5_per_intellect * floor( intellect() );
}

// player_t::composite_attack_power_multiplier =============================

double player_t::composite_attack_power_multiplier() SC_CONST
{
  double m = attack_power_multiplier;

  if ( ! is_pet() )
  {
    if ( ( sim -> auras.trueshot -> up() && ! is_enemy() && ! is_add() ) || buffs.blessing_of_might -> up() )
    {
      m *= 1.10;
    }
    else if ( ! is_enemy() && ! is_add() )
    {
      m *= 1.0 + std::max( sim -> auras.unleashed_rage -> value(), sim -> auras.abominations_might -> value() );
    }
  }

  return m;
}

// player_t::composite_attribute_multiplier ================================

double player_t::composite_attribute_multiplier( int attr ) SC_CONST
{
  double m = attribute_multiplier[ attr ];

  // MotW / BoK
  // ... increasing Strength, Agility, Stamina, and Intellect by 5%
  if ( attr == ATTR_STRENGTH || attr ==  ATTR_AGILITY || attr ==  ATTR_STAMINA || attr ==  ATTR_INTELLECT )
    if ( buffs.blessing_of_kings -> up() || buffs.mark_of_the_wild -> up() )
      m *= 1.05;

  if ( attr == ATTR_SPIRIT )
    m *= 1.0 + buffs.mana_tide -> value();

  return m;
}

// player_t::composite_player_multiplier ================================

double player_t::composite_player_multiplier( const school_type school, action_t* a ) SC_CONST
{
  double m = 1.0;

  if ( type != PLAYER_GUARDIAN )
  {
    if ( buffs.hellscreams_warsong -> up() || buffs.strength_of_wrynn -> up() )
    {
      // ICC buff.
      m *= 1.30;
    }

    if ( buffs.corruption_absolute -> up() )
      m *= 2.0;

    if ( buffs.tricks_of_the_trade -> check() )
    {
      // because of the glyph we now track the damage % increase in the buff value
      m *= 1.0 + buffs.tricks_of_the_trade -> value();
    }

    if ( ! is_pet() && ! is_enemy() && ! is_add() )
      if ( sim -> auras.ferocious_inspiration -> up() ||
           sim -> auras.communion             -> up() ||
           sim -> auras.arcane_tactics        -> up() )
      {
        m *= 1.03;
      }
  }

  if ( ( race == RACE_TROLL ) && ( sim -> target -> race == RACE_BEAST ) )
  {
    m *= 1.05;
  }

  return m;
}

// player_t::composite_player_td_multiplier ==============================

double player_t::composite_player_td_multiplier( const school_type school, action_t* a ) SC_CONST
{
  double m = 1.0;

  m *= 1.0 + buffs.dark_intent -> value() * buffs.dark_intent_feedback -> stack();

  return m;
}

// player_t::composite_player_heal_multiplier ================================

double player_t::composite_player_heal_multiplier( const school_type /* school */ ) SC_CONST
{
  double m = 1.0;

  if ( type != PLAYER_GUARDIAN )
  {
    if ( buffs.hellscreams_warsong -> up() || buffs.strength_of_wrynn -> up() )
    {
      // ICC buff.
      m *= 1.30;
    }
  }

  return m;
}

// player_t::composite_player_th_multiplier ==============================

double player_t::composite_player_th_multiplier( const school_type /* school */ ) SC_CONST
{
  double m = 1.0;

  m *= 1.0 + buffs.dark_intent -> value() * buffs.dark_intent_feedback -> stack();

  return m;
}

// player_t::composite_player_absorb_multiplier ================================

double player_t::composite_player_absorb_multiplier( const school_type /* school */ ) SC_CONST
{
  double m = 1.0;

  if ( type != PLAYER_GUARDIAN )
  {
    if ( buffs.hellscreams_warsong -> up() || buffs.strength_of_wrynn -> up() )
    {
      // ICC buff.
      m *= 1.30;
    }
  }

  return m;
}

// player_t::composite_movement_speed =====================================

double player_t::composite_movement_speed() SC_CONST
{
  double speed = base_movement_speed;

  speed *= 1.0 + buffs.body_and_soul -> value();

  // From http://www.wowpedia.org/Movement_speed_effects
  // Additional items looked up

  // Pursuit of Justice, Quickening: 8%/15%

  // DK: Unholy Presence: 15%

  // Druid: Feral Swiftness: 15%/30%

  // Aspect of  the Cheetah/Pack: 30%, with talent Pathfinding +34%/38%

  // Shaman Ghost Wolf: 30%, with Glyph 35%

  // Druid: Travel Form 40%

  // Druid: Dash: 50/60/70

  // Mage: Blazing Speed: 5%/10% chance after being hit for 50% for 8 sec
  //       Improved Blink: 35%/70% for 3 sec after blink
  //       Glyph of Invisibility: 40% while invisible

  // Body and Soul: 30%/60%

  // Rogue: Spring 70%

  // Swiftness Potion: 50%

  return speed;
}

// player_t::strength() ====================================================

double player_t::strength() SC_CONST
{
  double a = attribute[ ATTR_STRENGTH ];

  if ( ! is_pet() && ! is_enemy() && ! is_add() )
  {
    a += std::max( std::max( sim -> auras.strength_of_earth -> value(),
                             sim -> auras.horn_of_winter    -> value() ),
                   std::max( sim -> auras.battle_shout      -> value(),
                             sim -> auras.roar_of_courage   -> value() ) );
  }

  a *= composite_attribute_multiplier( ATTR_STRENGTH );

  return a;
}

// player_t::agility() =====================================================

double player_t::agility() SC_CONST
{
  double a = attribute[ ATTR_AGILITY ];

  if ( ! is_pet() && ! is_enemy() && ! is_add() )
  {
    a += std::max( std::max( sim -> auras.strength_of_earth -> value(),
                             sim -> auras.horn_of_winter    -> value() ),
                   std::max( sim -> auras.battle_shout      -> value(),
                             sim -> auras.roar_of_courage   -> value() ) );
  }

  a *= composite_attribute_multiplier( ATTR_AGILITY );

  return a;
}

// player_t::stamina() =====================================================

double player_t::stamina() SC_CONST
{
  double a = attribute[ ATTR_STAMINA ];

  if ( ! is_pet() && ! is_enemy() && ! is_add() )
  {
    a += sim -> auras.qiraji_fortitude -> value();
  }

  a *= composite_attribute_multiplier( ATTR_STAMINA );

  return a;
}

// player_t::intellect() ===================================================

double player_t::intellect() SC_CONST
{
  double a = attribute[ ATTR_INTELLECT ];

  a *= composite_attribute_multiplier( ATTR_INTELLECT );

  return a;
}

// player_t::spirit() ======================================================

double player_t::spirit() SC_CONST
{
  double a = attribute[ ATTR_SPIRIT ];

  if ( race == RACE_HUMAN )
  {
    a += ( a - attribute_base[ ATTR_SPIRIT ] ) * 0.03;
  }

  a *= composite_attribute_multiplier( ATTR_SPIRIT );

  return a;
}

/*
// player_t::haste_rating() ======================================================

double player_t::haste_rating() SC_CONST
{
  double a = stats.haste_rating;

  return a;
}


// player_t::crit_rating() ======================================================

double player_t::crit_rating() SC_CONST
{
  double a = stats.crit_rating;

  return a;
}


// player_t::mastery_rating() ======================================================

double player_t::mastery_rating() SC_CONST
{
  double a = stats.mastery_rating;

  return a;
}


// player_t::hit_rating() ======================================================

double player_t::hit_rating() SC_CONST
{
  double a = stats.hit_rating;

  return a;
}


// player_t::expertise_rating() ======================================================

double player_t::expertise_rating() SC_CONST
{
  double a = stats.expertise_rating;

  return a;
}


// player_t::dodge_rating() ======================================================

double player_t::dodge_rating() SC_CONST
{
  double a = stats.dodge_rating;
  
  return a;
}


// player_t::parry_rating() ======================================================

double player_t::parry_rating() SC_CONST
{
  double a = stats.parry_rating;

  a += strength() * parry_rating_per_strength;

  return a;
}
*/

// player_t::combat_begin ==================================================

void player_t::combat_begin( sim_t* sim )
{
  player_t::death_knight_combat_begin( sim );
  player_t::druid_combat_begin       ( sim );
  player_t::hunter_combat_begin      ( sim );
  player_t::mage_combat_begin        ( sim );
  player_t::paladin_combat_begin     ( sim );
  player_t::priest_combat_begin      ( sim );
  player_t::rogue_combat_begin       ( sim );
  player_t::shaman_combat_begin      ( sim );
  player_t::warlock_combat_begin     ( sim );
  player_t::warrior_combat_begin     ( sim );
}

// player_t::combat_begin ==================================================

void player_t::combat_begin()
{
  if ( sim -> debug ) log_t::output( sim, "Combat begins for player %s", name() );

  if ( ! is_pet() && !is_add() )
  {
    arise();
  }

  if ( sim -> overrides.essence_of_the_red )
  {
    buffs.essence_of_the_red -> trigger();
  }
  if ( sim -> overrides.corruption_absolute )
  {
    buffs.corruption_absolute -> trigger();
  }
  if ( sim -> overrides.strength_of_wrynn )
  {
    buffs.strength_of_wrynn -> trigger();
  }
  if ( sim -> overrides.hellscreams_warsong )
  {
    buffs.hellscreams_warsong -> trigger();
  }

  if ( race == RACE_DRAENEI )
  {
    buffs.heroic_presence -> trigger();
  }

  if ( sim -> overrides.replenishment )
    buffs.replenishment -> override();

  init_resources( true );

  if ( primary_resource() == RESOURCE_MANA )
  {
    get_gain( "initial_mana" ) -> add( resource_max[ RESOURCE_MANA ] );
    get_gain( "initial_mana" ) -> type = RESOURCE_MANA;
  }

  if ( primary_role() == ROLE_TANK && !is_enemy() && !is_add() )
    new ( sim ) vengeance_t( this );

  action_sequence = "";
}

// player_t::combat_end ====================================================

void player_t::combat_end( sim_t* sim )
{
  player_t::death_knight_combat_end( sim );
  player_t::druid_combat_end       ( sim );
  player_t::hunter_combat_end      ( sim );
  player_t::mage_combat_end        ( sim );
  player_t::paladin_combat_end     ( sim );
  player_t::priest_combat_end      ( sim );
  player_t::rogue_combat_end       ( sim );
  player_t::shaman_combat_end      ( sim );
  player_t::warlock_combat_end     ( sim );
  player_t::warrior_combat_end     ( sim );
}

// player_t::combat_end ====================================================

void player_t::combat_end()
{
  if ( sim -> debug ) log_t::output( sim, "Combat ends for player %s", name() );

  if ( ! is_pet() )
  {
    demise();
  }
  else if ( is_pet() )
    cast_pet() -> dismiss();



  double iteration_seconds = current_time;

  if ( iteration_seconds > 0 )
  {
    total_seconds += iteration_seconds;

    for ( pet_t* pet = pet_list; pet; pet = pet -> next_pet )
    {
      iteration_dmg += pet -> iteration_dmg;
    }
    iteration_dps[ sim -> current_iteration ] = iteration_dmg / iteration_seconds;
  }
}

// player_t::reset =========================================================

void player_t::reset()
{
  if ( sim -> debug ) log_t::output( sim, "Reseting player %s", name() );

  skill = initial_skill;

  last_cast = 0;
  gcd_ready = 0;

  sleeping = 1;

  dmg_taken = 0;

  stats = initial_stats;

  vengeance_damage = vengeance_value = vengeance_max = 0.0;

  haste_rating = initial_haste_rating;
  mastery_rating = initial_mastery_rating;
  mastery = base_mastery + mastery_rating / rating.mastery;
  recalculate_haste();

  for ( int i=0; i < ATTRIBUTE_MAX; i++ )
  {
    attribute           [ i ] = attribute_initial           [ i ];
    attribute_multiplier[ i ] = attribute_multiplier_initial[ i ];
    // Matched gear. i.e. Mysticism etc.
    if ( ( level >= 50 ) && matching_gear )
      attribute_multiplier[ i ] *= 1.0 + matching_gear_multiplier( ( const attribute_type ) i );
  }

  for ( int i=0; i <= SCHOOL_MAX; i++ )
  {
    spell_power[ i ] = initial_spell_power[ i ];
    resource_reduction[ i ] = initial_resource_reduction[ i ];
  }

  spell_hit         = initial_spell_hit;
  spell_crit        = initial_spell_crit;
  spell_penetration = initial_spell_penetration;
  mp5               = initial_mp5;

  attack_power       = initial_attack_power;
  attack_hit         = initial_attack_hit;
  attack_expertise   = initial_attack_expertise;
  attack_crit        = initial_attack_crit;

  armor              = initial_armor;
  bonus_armor        = initial_bonus_armor;
  dodge              = initial_dodge;
  parry              = initial_parry;
  block              = initial_block;

  spell_power_multiplier    = initial_spell_power_multiplier;
  spell_power_per_intellect = initial_spell_power_per_intellect;
  spell_power_per_spirit    = initial_spell_power_per_spirit;
  spell_crit_per_intellect  = initial_spell_crit_per_intellect;

  attack_power_multiplier   = initial_attack_power_multiplier;
  attack_power_per_strength = initial_attack_power_per_strength;
  attack_power_per_agility  = initial_attack_power_per_agility;
  attack_crit_per_agility   = initial_attack_crit_per_agility;

  armor_multiplier          = initial_armor_multiplier;
  dodge_per_agility         = initial_dodge_per_agility;
  parry_rating_per_strength = initial_parry_rating_per_strength;

  for ( buff_t* b = buff_list; b; b = b -> next )
  {
    b -> reset();
  }

  last_foreground_action = 0;
  current_time = 0;

  executing = 0;
  channeling = 0;
  readying = 0;
  in_combat = false;
  iteration_dmg = 0;
  
  cast_delay_reaction = 0;
  cast_delay_occurred = 0;

  main_hand_weapon.buff_type  = 0;
  main_hand_weapon.buff_value = 0;
  main_hand_weapon.bonus_dmg  = 0;

  off_hand_weapon.buff_type  = 0;
  off_hand_weapon.buff_value = 0;
  off_hand_weapon.bonus_dmg  = 0;

  ranged_weapon.buff_type  = 0;
  ranged_weapon.buff_value = 0;
  ranged_weapon.bonus_dmg  = 0;

  elixir_battle   = ELIXIR_NONE;
  elixir_guardian = ELIXIR_NONE;
  flask           = FLASK_NONE;
  food            = FOOD_NONE;

  for ( int i=0; i < RESOURCE_MAX; i++ )
  {
    action_callback_t::reset( resource_gain_callbacks[ i ] );
    action_callback_t::reset( resource_loss_callbacks[ i ] );
  }
  for ( int i=0; i < RESULT_MAX; i++ )
  {
    action_callback_t::reset( attack_callbacks[ i ] );
    action_callback_t::reset( spell_callbacks [ i ] );
    action_callback_t::reset( tick_callbacks  [ i ] );
  }
  for ( int i=0; i < SCHOOL_MAX; i++ )
  {
    action_callback_t::reset( tick_damage_callbacks  [ i ] );
    action_callback_t::reset( direct_damage_callbacks[ i ] );
  }

  replenishment_targets.clear();

  init_resources( true );

  for ( action_t* a = action_list; a; a = a -> next ) a -> reset();

  for ( cooldown_t* c = cooldown_list; c; c = c -> next ) c -> reset();

  for ( dot_t* d = dot_list; d; d = d -> next ) d -> reset();

  for ( stats_t* s = stats_list; s; s = s -> next ) s -> reset();

  potion_used = 0;
}

// player_t::schedule_ready =================================================

void player_t::schedule_ready( double delta_time,
                               bool   waiting )
{
  assert( ! readying );
  action_t* was_executing = ( channeling ? channeling : executing );

  executing = 0;
  channeling = 0;
  action_queued = false;

  if ( ! has_foreground_actions( this ) ) return;

  double gcd_adjust = gcd_ready - ( sim -> current_time + delta_time );
  if ( gcd_adjust > 0 ) delta_time += gcd_adjust;

  if ( waiting )
  {
    total_waiting += delta_time;
  }
  else
  {
    double lag = 0;

    if ( last_foreground_action && ! last_foreground_action -> auto_cast )
    {
      if ( last_foreground_action -> ability_lag > 0.0 )
      {
        double ability_lag = rngs.lag_ability -> gauss( last_foreground_action -> ability_lag, last_foreground_action -> ability_lag_stddev );
        double gcd_lag     = rngs.lag_gcd   -> gauss( sim ->   gcd_lag, sim ->   gcd_lag_stddev );
        double diff        = ( gcd_ready + gcd_lag ) - ( sim -> current_time + ability_lag );
        if ( diff > 0 && sim -> strict_gcd_queue )
        {
          lag = gcd_lag;
        }
        else
        {
          lag = ability_lag;
          action_queued = true;
        }
      }
      else if ( last_foreground_action -> gcd() == 0 )
      {
        lag = 0;
      }
      else if ( last_foreground_action -> channeled )
      {
        lag = rngs.lag_channel -> gauss( sim -> channel_lag, sim -> channel_lag_stddev );
      }
      else
      {
        double   gcd_lag = rngs.lag_gcd   -> gauss( sim ->   gcd_lag, sim ->   gcd_lag_stddev );
        double queue_lag = rngs.lag_queue -> gauss( sim -> queue_lag, sim -> queue_lag_stddev );

        double diff = ( gcd_ready + gcd_lag ) - ( sim -> current_time + queue_lag );

        if ( diff > 0 && sim -> strict_gcd_queue )
        {
          lag = gcd_lag;
        }
        else
        {
          lag = queue_lag;
          action_queued = true;
        }
      }
    }

    if ( lag < 0 ) lag = 0;

    delta_time += lag;
  }

  if ( last_foreground_action )
  {
    last_foreground_action -> stats -> total_execute_time += delta_time;
  }

  if ( delta_time == 0 ) delta_time = 0.000001;

  readying = new ( sim ) player_ready_event_t( sim, this, delta_time );

  if ( was_executing && was_executing -> gcd() > 0 && ! was_executing -> background && ! was_executing -> proc && ! was_executing -> repeating )
  {
    // Record the last ability use time for cast_react
    cast_delay_occurred = readying -> occurs();
    cast_delay_reaction = rngs.lag_brain -> gauss( brain_lag, brain_lag_stddev );
    if ( sim -> debug ) 
    {
      log_t::output( sim, "%s %s schedule_ready(): cast_finishes=%f cast_delay=%f", 
        name_str.c_str(),
        was_executing -> name_str.c_str(), 
        readying -> occurs(), 
        cast_delay_reaction );
    }
  }
}

// player_t::arise ==========================================================

void player_t::arise()
{
  if ( sim -> log )
    log_t::output( sim, "%s arises.", name() );

  sleeping = 0;

  init_resources( true );

  readying = 0;

  schedule_ready();
}

// player_t::demise =========================================================

void player_t::demise()
{
  if ( sim -> log )
    log_t::output( sim, "%s demises.", name() );

  sleeping = 1;
  readying = 0;

  for( buff_t* b = buff_list; b; b = b -> next )
  {
    b -> expire();
  }
  for ( action_t* a = action_list; a; a = a -> next )
  {
    a -> cancel();
  }

  sim -> cancel_events( this );

  for ( pet_t* pet = pet_list; pet; pet = pet -> next_pet )
  {
    pet -> demise();
  }
}

// player_t::interrupt ======================================================

void player_t::interrupt()
{
  // FIXME! Players will need to override this to handle background repeating actions.

  if ( sim -> log ) log_t::output( sim, "%s is interrupted", name() );

  if ( executing  ) executing  -> interrupt_action();
  if ( channeling ) channeling -> interrupt_action();

  if ( buffs.stunned -> check() )
  {
    if ( readying ) event_t::cancel( readying );
  }
  else
  {
    if ( ! readying && ! sleeping ) schedule_ready();
  }
}

// player_t::halt ===========================================================

void player_t::halt()
{
  if ( sim -> log ) log_t::output( sim, "%s is halted", name() );

  interrupt();

  if ( main_hand_attack ) main_hand_attack -> cancel();
  if (  off_hand_attack )  off_hand_attack -> cancel();
  if (    ranged_attack )    ranged_attack -> cancel();
}

// player_t::stun() =========================================================

void player_t::stun()
{
  halt();
}

// player_t::moving =========================================================

void player_t::moving()
{
  // FIXME! In the future, some movement events may not cause auto-attack to stop.

  halt();
}

// player_t::clear_debuffs===================================================

void player_t::clear_debuffs()
{
  // FIXME! At the moment we are just clearing DoTs

  if ( sim -> log ) log_t::output( sim, "%s clears debuffs from %s", name(), sim -> target -> name() );

  for ( action_t* a = action_list; a; a = a -> next )
  {
    if ( a -> dot -> ticking ) a -> cancel();
  }
}

// player_t::execute_action =================================================
std::string player_t::print_action_map( int iterations, int precision )
{
  std::map<std::string,int>::const_iterator it = action_map.begin();
  std::string ret = "Label: Number of executes (Average number of executes per iteration)";
  ret += "<br />\n";
  while ( it != action_map.end() )
  {
    ret += it->first + ": " + util_t::to_string( it -> second );
    if ( iterations > 0 ) ret += " (" + util_t::to_string( ( ( double )it -> second ) / iterations, precision ) + ")";
    ret += "<br />\n";
    it++;
  }

  return ret;
}

action_t* player_t::execute_action()
{
  readying = 0;

  action_t* action=0;

  for ( action = action_list; action; action = action -> next )
  {
    if ( action -> background ||
         action -> sequence )
      continue;

    if ( action -> ready() )
      break;

    if ( action -> wait_on_ready == 1 )
    {
      action = 0;
      break;
    }
  }

  last_foreground_action = action;

  if ( action )
  {
    action -> schedule_execute();
    total_foreground_actions++;
    if ( action -> marker ) action_sequence += action -> marker;
    if ( action -> label_str != "" )
    {
      if ( ! action_map[ action -> label_str ] )
        action_map[ action -> label_str ] = 0;

      action_map [ action -> label_str ] += 1;
    }
  }

  return action;
}

// player_t::regen =========================================================

void player_t::regen( double periodicity )
{
  int resource_type = primary_resource();


    if ( resource_type == RESOURCE_ENERGY )
    {
      double energy_regen = periodicity * energy_regen_per_second();

      resource_gain( RESOURCE_ENERGY, energy_regen, gains.energy_regen );
    }
    else if ( resource_type == RESOURCE_FOCUS )
    {
      double focus_regen = periodicity * focus_regen_per_second();

      resource_gain( RESOURCE_FOCUS, focus_regen, gains.focus_regen );
    }
    else if ( resource_type == RESOURCE_MANA )
    {
      if( buffs.innervate -> check() )
      {
        resource_gain( RESOURCE_MANA, buffs.innervate -> value() * periodicity, gains.innervate );
      }

      double spirit_regen = periodicity * sqrt( floor( intellect() ) ) * floor( spirit() ) * mana_regen_base;

      if ( mana_regen_while_casting < 1.0 )
      {
        spirit_regen *= mana_regen_while_casting;
      }
      if( spirit_regen > 0 )
      {
        resource_gain( RESOURCE_MANA, spirit_regen, gains.spirit_intellect_regen );
      }

      double mp5_regen = periodicity * composite_mp5() / 5.0;

      resource_gain( RESOURCE_MANA, mp5_regen, gains.mp5_regen );

      if ( buffs.replenishment -> up() )
      {
        double replenishment_regen = periodicity * resource_max[ RESOURCE_MANA ] * 0.0010 / 1.0;

        resource_gain( RESOURCE_MANA, replenishment_regen, gains.replenishment );
      }

      if ( buffs.essence_of_the_red -> up() )
      {
        double essence_regen = periodicity * resource_max[ RESOURCE_MANA ] * 0.05;

        resource_gain( RESOURCE_MANA, essence_regen, gains.essence_of_the_red );
      }

      double bow = buffs.blessing_of_might_regen -> current_value;
      double ms  = ( ! is_enemy() && ! is_add() ) ? sim -> auras.mana_spring_totem -> current_value : 0;

      if ( ms > bow )
      {
        double mana_spring_regen = periodicity * ms / 5.0;

        resource_gain( RESOURCE_MANA, mana_spring_regen, gains.mana_spring_totem );
      }
      else if ( bow > 0 )
      {
        double wisdom_regen = periodicity * bow / 5.0;

        resource_gain( RESOURCE_MANA, wisdom_regen, gains.blessing_of_might );
      }
    }


  if ( resource_type != RESOURCE_NONE )
  {
    int index = ( int ) sim -> current_time;
    int size = ( int ) timeline_resource.size();

    if ( index >= size ) timeline_resource.insert( timeline_resource.begin() + size, size, 0 );

    timeline_resource[ index ] += resource_current[ resource_type ] * periodicity;
  }
  if ( resource_type != RESOURCE_HEALTH )
  {
    int index = ( int ) sim -> current_time;
    int size = ( int ) timeline_health.size();

    if ( index >= size ) timeline_health.insert( timeline_health.begin() + size, size, 0 );

    timeline_health[ index ] += resource_current[ RESOURCE_HEALTH ] * periodicity;
  }
}

// player_t::resource_loss =================================================

double player_t::resource_loss( int       resource,
                                double    amount,
                                action_t* action )
{
  if ( amount == 0 )
    return 0;

  if ( sleeping )
    return 0;

  double actual_amount;

  if ( infinite_resource[ resource ] == 0 || is_enemy() )
  {
    actual_amount = std::min( amount, resource_current[ resource ] );
    resource_current[ resource ] -= actual_amount;
    resource_lost[ resource ] += actual_amount;
  }
  else
  {
    actual_amount = amount;
    resource_current[ resource ] -= actual_amount;
    resource_lost[ resource ] += actual_amount;
  }

  if ( resource == RESOURCE_MANA )
  {
    last_cast = sim -> current_time;
  }

  if ( action ) action_callback_t::trigger( resource_loss_callbacks[ resource ], action, ( void* ) &actual_amount );

  if ( sim -> debug )
    log_t::output( sim, "Player %s loses %.2f (%.2f) %s",
                   name(), actual_amount, amount, util_t::resource_type_string( resource ) );

  return actual_amount;
}

// player_t::resource_gain =================================================

double player_t::resource_gain( int       resource,
                                double    amount,
                                gain_t*   source,
                                action_t* action )
{
  if ( sleeping )
    return 0;

  double actual_amount = std::min( amount, resource_max[ resource ] - resource_current[ resource ] );

  if ( actual_amount > 0 )
  {
    resource_current[ resource ] += actual_amount;
  }

  resource_gained [ resource ] += actual_amount;

  if ( source )
  {
    if ( source -> type == RESOURCE_NONE )
      source -> type = ( resource_type ) resource;
    if ( resource != source -> type )
    {
      sim -> errorf( "player_t::resource_gain: player=%s gain=%s resource_gain type not identical to gain resource type..\n resource=%s gain=%s",
                     name(), source -> name_str.c_str(), util_t::resource_type_string( resource ), util_t::resource_type_string( source -> type ) );
      assert ( 0 );
    }
    source -> add( actual_amount, amount - actual_amount );
  }

  if ( action ) action_callback_t::trigger( resource_gain_callbacks[ resource ], action, ( void* ) &actual_amount );

  if ( sim -> log )
  {
    log_t::output( sim, "%s gains %.2f (%.2f) %s from %s (%.2f)",
                   name(), actual_amount, amount,
                   util_t::resource_type_string( resource ),
                   source ? source -> name() : action ? action -> name() : "unknown",
                   resource_current[ resource ] );
  }

  return actual_amount;
}

// player_t::resource_available ============================================

bool player_t::resource_available( int    resource,
                                   double cost ) SC_CONST
{
  if ( resource == RESOURCE_NONE || cost == 0 || infinite_resource[ resource ] == 1 )
  {
    return true;
  }

  return resource_current[ resource ] >= cost;
}

// player_t::primary_tab ===================================================

int player_t::primary_tab()
{
  specialization = TALENT_TAB_NONE;

  int max_points = 0;

  for( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    if ( talent_tab_points[ i ] > 0 )
    {
      if( specialization == TALENT_TAB_NONE || ( talent_tab_points[ i ] > max_points ) )
      {
        specialization = i;
        max_points = talent_tab_points[ i ];
      }
    }
  }

  return specialization;
}

// player_t::primary_role ===================================================

int player_t::primary_role() SC_CONST
{
  return role;
}

// player_t::primary_tree_name ==============================================

const char* player_t::primary_tree_name() SC_CONST
{
  return util_t::talent_tree_string( primary_tree() );
}

// player_t::primary_tree ===================================================

int player_t::primary_tree() SC_CONST
{
  if ( specialization == TALENT_TAB_NONE )
    return TREE_NONE;

  return tree_type[ specialization ];
}

// player_t::normalize_by ===================================================

int player_t::normalize_by() SC_CONST
{
  if ( sim -> normalized_stat != STAT_NONE )
  {
    return sim -> normalized_stat;
  }

  if ( primary_role() == ROLE_SPELL || primary_role() == ROLE_HEAL )
    return STAT_INTELLECT;
  else if ( primary_role() == ROLE_TANK )
    return STAT_ARMOR;
  else if ( type == DRUID || type == HUNTER || type == SHAMAN || type == ROGUE )
    return STAT_AGILITY;
  else if ( type == DEATH_KNIGHT || type == PALADIN || type == WARRIOR )
    return STAT_STRENGTH;

  return STAT_ATTACK_POWER;
}

// player_t::health_percentage() ===================================================

double player_t::health_percentage() SC_CONST
{
  return resource_current[ RESOURCE_HEALTH ] / resource_max[ RESOURCE_HEALTH ] * 100 ;
}

// target_t::time_to_die =====================================================

double player_t::time_to_die() SC_CONST
{
  if ( resource_base[ RESOURCE_HEALTH ] > 0 )
  {
    return sim -> current_time * resource_current[ RESOURCE_HEALTH ] / dmg_taken;
  }
  else
  {
    return sim -> expected_time - sim -> current_time;
  }
}

// player_t::total_reaction_time ============================================

double player_t::total_reaction_time() SC_CONST
{

  return rngs.lag_reaction -> exgauss( reaction_mean, reaction_stddev, reaction_nu );
}

// player_t::stat_gain ======================================================

void player_t::stat_gain( int       stat,
                          double    amount,
                          gain_t*   gain,
                          action_t* action )
{
  if( amount <= 0 ) return;

  if ( sim -> log ) log_t::output( sim, "%s gains %.0f %s", name(), amount, util_t::stat_type_string( stat ) );

  switch ( stat )
  {
  case STAT_STRENGTH:  stats.attribute[ ATTR_STRENGTH  ] += amount; attribute[ ATTR_STRENGTH  ] += amount; break;
  case STAT_AGILITY:   stats.attribute[ ATTR_AGILITY   ] += amount; attribute[ ATTR_AGILITY   ] += amount; break;
  case STAT_STAMINA:   stats.attribute[ ATTR_STAMINA   ] += amount; attribute[ ATTR_STAMINA   ] += amount; resource_max[ RESOURCE_HEALTH ] += amount * health_per_stamina; break;
  case STAT_INTELLECT: stats.attribute[ ATTR_INTELLECT ] += amount; attribute[ ATTR_INTELLECT ] += amount; resource_max[ RESOURCE_MANA   ] += amount * mana_per_intellect; break;
  case STAT_SPIRIT:    stats.attribute[ ATTR_SPIRIT    ] += amount; attribute[ ATTR_SPIRIT    ] += amount; break;

  case STAT_MAX: for( int i=0; i < ATTRIBUTE_MAX; i++ ) { stats.attribute[ i ] += amount; attribute[ i ] += amount; } break;

  case STAT_HEALTH: resource_gain( RESOURCE_HEALTH, amount, gain, action ); break;
  case STAT_MANA:   resource_gain( RESOURCE_MANA,   amount, gain, action ); break;
  case STAT_RAGE:   resource_gain( RESOURCE_RAGE,   amount, gain, action ); break;
  case STAT_ENERGY: resource_gain( RESOURCE_ENERGY, amount, gain, action ); break;
  case STAT_FOCUS:  resource_gain( RESOURCE_FOCUS,  amount, gain, action ); break;
  case STAT_RUNIC:  resource_gain( RESOURCE_RUNIC,  amount, gain, action ); break;

  case STAT_MAX_HEALTH: resource_max[ RESOURCE_HEALTH ] += amount; resource_gain( RESOURCE_HEALTH, amount, gain, action ); break;
  case STAT_MAX_MANA:   resource_max[ RESOURCE_MANA   ] += amount; resource_gain( RESOURCE_MANA,   amount, gain, action ); break;
  case STAT_MAX_RAGE:   resource_max[ RESOURCE_RAGE   ] += amount; resource_gain( RESOURCE_RAGE,   amount, gain, action ); break;
  case STAT_MAX_ENERGY: resource_max[ RESOURCE_ENERGY ] += amount; resource_gain( RESOURCE_ENERGY, amount, gain, action ); break;
  case STAT_MAX_FOCUS:  resource_max[ RESOURCE_FOCUS  ] += amount; resource_gain( RESOURCE_FOCUS,  amount, gain, action ); break;
  case STAT_MAX_RUNIC:  resource_max[ RESOURCE_RUNIC  ] += amount; resource_gain( RESOURCE_RUNIC,  amount, gain, action ); break;

  case STAT_SPELL_POWER:       stats.spell_power       += amount; spell_power[ SCHOOL_MAX ] += amount; break;
  case STAT_SPELL_PENETRATION: stats.spell_penetration += amount; spell_penetration         += amount; break;
  case STAT_MP5:               stats.mp5               += amount; mp5                       += amount; break;

  case STAT_ATTACK_POWER:             stats.attack_power             += amount; attack_power       += amount;                            break;
  case STAT_EXPERTISE_RATING:         stats.expertise_rating         += amount; attack_expertise   += amount / rating.expertise;         break;

  case STAT_HIT_RATING:
    stats.hit_rating += amount;
    attack_hit       += amount / rating.attack_hit;
    spell_hit        += amount / rating.spell_hit;
    break;

  case STAT_CRIT_RATING:
    stats.crit_rating += amount;
    attack_crit       += amount / rating.attack_crit;
    spell_crit        += amount / rating.spell_crit;
    break;

  case STAT_HASTE_RATING:
    stats.haste_rating += amount;
    haste_rating       += amount;
    recalculate_haste();
    break;

  case STAT_ARMOR:          stats.armor          += amount; armor       += amount;                  break;
  case STAT_BONUS_ARMOR:    stats.bonus_armor    += amount; bonus_armor += amount;                  break;
  case STAT_DODGE_RATING:   stats.dodge_rating   += amount; dodge       += amount / rating.dodge;   break;
  case STAT_PARRY_RATING:   stats.parry_rating   += amount; parry       += amount / rating.parry;   break;

  case STAT_BLOCK_RATING: stats.block_rating += amount; block       += amount / rating.block; break;

  case STAT_MASTERY_RATING:
    stats.mastery_rating += amount;
    mastery += amount / rating.mastery;
    break;

  default: assert( 0 );
  }
}

// player_t::stat_loss ======================================================

void player_t::stat_loss( int       stat,
                          double    amount,
                          action_t* action )
{
  if( amount <= 0 ) return;

  if ( sim -> log ) log_t::output( sim, "%s loses %.0f %s", name(), amount, util_t::stat_type_string( stat ) );

  switch ( stat )
  {
  case STAT_STRENGTH:  stats.attribute[ ATTR_STRENGTH  ] -= amount; attribute[ ATTR_STRENGTH  ] -= amount; break;
  case STAT_AGILITY:   stats.attribute[ ATTR_AGILITY   ] -= amount; attribute[ ATTR_AGILITY   ] -= amount; break;
  case STAT_STAMINA:   stats.attribute[ ATTR_STAMINA   ] -= amount; attribute[ ATTR_STAMINA   ] -= amount; stat_loss( STAT_MAX_HEALTH, amount * health_per_stamina, action ); break;
  case STAT_INTELLECT: stats.attribute[ ATTR_INTELLECT ] -= amount; attribute[ ATTR_INTELLECT ] -= amount; stat_loss( STAT_MAX_MANA,   amount * mana_per_intellect, action ); break;
  case STAT_SPIRIT:    stats.attribute[ ATTR_SPIRIT    ] -= amount; attribute[ ATTR_SPIRIT    ] -= amount; break;

  case STAT_MAX: for( int i=0; i < ATTRIBUTE_MAX; i++ ) { stats.attribute[ i ] -= amount; attribute[ i ] -= amount; } break;

  case STAT_HEALTH: resource_loss( RESOURCE_HEALTH, amount, action ); break;
  case STAT_MANA:   resource_loss( RESOURCE_MANA,   amount, action ); break;
  case STAT_RAGE:   resource_loss( RESOURCE_RAGE,   amount, action ); break;
  case STAT_ENERGY: resource_loss( RESOURCE_ENERGY, amount, action ); break;
  case STAT_FOCUS:  resource_loss( RESOURCE_FOCUS,  amount, action ); break;
  case STAT_RUNIC:  resource_loss( RESOURCE_RUNIC,  amount, action ); break;

  case STAT_MAX_HEALTH:
  case STAT_MAX_MANA:
  case STAT_MAX_RAGE:
  case STAT_MAX_ENERGY:
  case STAT_MAX_FOCUS:
  case STAT_MAX_RUNIC:
  {
    int r = ( ( stat == STAT_MAX_HEALTH ) ? RESOURCE_HEALTH :
              ( stat == STAT_MAX_MANA   ) ? RESOURCE_MANA   :
              ( stat == STAT_MAX_RAGE   ) ? RESOURCE_RAGE   :
              ( stat == STAT_MAX_ENERGY ) ? RESOURCE_ENERGY :
              ( stat == STAT_MAX_FOCUS  ) ? RESOURCE_FOCUS  : RESOURCE_RUNIC );
    resource_max[ r ] -= amount;
    double delta = resource_current[ r ] - resource_max[ r ];
    if( delta > 0 ) resource_loss( r, delta, action );
  }
  break;

  case STAT_SPELL_POWER:       stats.spell_power       -= amount; spell_power[ SCHOOL_MAX ] -= amount; break;
  case STAT_SPELL_PENETRATION: stats.spell_penetration -= amount; spell_penetration         -= amount; break;
  case STAT_MP5:               stats.mp5               -= amount; mp5                       -= amount; break;

  case STAT_ATTACK_POWER:             stats.attack_power             -= amount; attack_power       -= amount;                            break;
  case STAT_EXPERTISE_RATING:         stats.expertise_rating         -= amount; attack_expertise   -= amount / rating.expertise;         break;

  case STAT_HIT_RATING:
    stats.hit_rating -= amount;
    attack_hit       -= amount / rating.attack_hit;
    spell_hit        -= amount / rating.spell_hit;
    break;

  case STAT_CRIT_RATING:
    stats.crit_rating -= amount;
    attack_crit       -= amount / rating.attack_crit;
    spell_crit        -= amount / rating.spell_crit;
    break;

  case STAT_HASTE_RATING:
    stats.haste_rating -= amount;
    haste_rating       -= amount;
    recalculate_haste();
    break;

  case STAT_ARMOR:          stats.armor          -= amount; armor       -= amount;                  break;
  case STAT_BONUS_ARMOR:    stats.bonus_armor    -= amount; bonus_armor -= amount;                  break;
  case STAT_DODGE_RATING:   stats.dodge_rating   -= amount; dodge       -= amount / rating.dodge;   break;
  case STAT_PARRY_RATING:   stats.parry_rating   -= amount; parry       -= amount / rating.parry;   break;

  case STAT_BLOCK_RATING: stats.block_rating -= amount; block       -= amount / rating.block; break;

  case STAT_MASTERY_RATING:
    stats.mastery_rating -= amount;
    mastery -= amount / rating.mastery;
    break;

  default: assert( 0 );
  }
}

// player_t::cost_reduction_gain ======================================================

void player_t::cost_reduction_gain( int       school,
                                    double    amount,
                                    gain_t*   gain,
                                    action_t* action )
{
  if( amount <= 0 ) return;

  if ( sim -> log ) log_t::output( sim, "%s gains a cost reduction of %.0f on abilities of school %s", name(), amount, util_t::school_type_string( school ) );

  if ( school > SCHOOL_MAX_PRIMARY )
  {
    for ( int i = 1; i < SCHOOL_MAX_PRIMARY; i++ )
    {
      if ( util_t::school_type_component( school, i ) )
      {
        resource_reduction[ i ] += amount;
      }
    }
  }
  else
  {
    resource_reduction[ school ] += amount;
  }
}

// player_t::cost_reduction_loss ======================================================

void player_t::cost_reduction_loss( int       school,
                                    double    amount,
                                    action_t* action )
{
  if( amount <= 0 ) return;

  if ( sim -> log ) log_t::output( sim, "%s loses a cost reduction %.0f on abilities of school %s", name(), amount, util_t::school_type_string( school ) );

  if ( school > SCHOOL_MAX_PRIMARY )
  {
    for ( int i = 1; i < SCHOOL_MAX_PRIMARY; i++ )
    {
      if ( util_t::school_type_component( school, i ) )
      {
        resource_reduction[ i ] -= amount;
      }
    }
  }
  else
  {
    resource_reduction[ school ] -= amount;
  }
}

// player_t::assess_damage ==================================================

double player_t::assess_damage( double            amount,
                                const school_type school,
                                int               dmg_type,
                                int               result,
                                action_t*         action )
{
  double mitigated_amount = target_mitigation( amount, school, dmg_type, result, action );

  dmg_taken += mitigated_amount;
  total_dmg_taken += mitigated_amount;

  double actual_amount = resource_loss( RESOURCE_HEALTH, mitigated_amount );

  if ( resource_current[ RESOURCE_HEALTH ] <= 0 && !is_enemy() )
  {
    if ( !sleeping )
    {
      death_count++;
      death_time.push_back( current_time );
    }
    if ( sim -> log ) log_t::output( sim, "%s has died.", name() );
    demise();
  }

  if ( vengeance_enabled )
    vengeance_damage += actual_amount;

  return mitigated_amount;
}

// player_t::target_mitigation ==============================================

double player_t::target_mitigation( double            amount,
                                    const school_type school,
                                    int               dmg_type,
                                    int               result,
                                    action_t*         action )
{
  double mitigated_amount = amount;

  if ( school == SCHOOL_PHYSICAL )
  {
    // Inspiration
    mitigated_amount *= 1.0 - buffs.inspiration -> value() / 100.0;

    // Armor
    if ( action )
    {
      double resist = action -> armor() / ( action -> armor() + action -> player -> armor_coeff );

      if ( resist < 0.0 )
        resist = 0.0;
      else if ( resist > 0.75 )
        resist = 0.75;
      mitigated_amount *= 1.0 - resist;
    }
  }

  return mitigated_amount;
}

// player_t::summon_pet =====================================================

void player_t::summon_pet( const char* pet_name,
                           double      duration )
{
  for ( pet_t* p = pet_list; p; p = p -> next_pet )
  {
    if ( p -> name_str == pet_name )
    {
      p -> summon( duration );
      return;
    }
  }
  sim -> errorf( "Player %s is unable to summon pet '%s'\n", name(), pet_name );
}

// player_t::dismiss_pet ====================================================

void player_t::dismiss_pet( const char* pet_name )
{
  for ( pet_t* p = pet_list; p; p = p -> next_pet )
  {
    if ( p -> name_str == pet_name )
    {
      p -> dismiss();
      return;
    }
  }
  assert( 0 );
}

// player_t::register_callbacks =============================================

void player_t::register_callbacks()
{
  dark_intent_cb = new dark_intent_callback_t( this );
  dark_intent_cb -> active = false;
  register_tick_callback( RESULT_CRIT_MASK, dark_intent_cb );
}

// player_t::register_resource_gain_callback ================================

void player_t::register_resource_gain_callback( int resource,
                                                action_callback_t* cb )
{
  resource_gain_callbacks[ resource ].push_back( cb );
}

// player_t::register_resource_loss_callback ================================

void player_t::register_resource_loss_callback( int resource,
                                                action_callback_t* cb )
{
  resource_loss_callbacks[ resource ].push_back( cb );
}

// player_t::register_attack_callback =======================================

void player_t::register_attack_callback( int64_t mask,
                                         action_callback_t* cb )
{
  for ( int64_t i=0; i < RESULT_MAX; i++ )
  {
    if ( ( i > 0 && mask < 0 ) || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      attack_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_spell_callback ========================================

void player_t::register_spell_callback( int64_t mask,
                                        action_callback_t* cb )
{
  for ( int64_t i=0; i < RESULT_MAX; i++ )
  {
    if ( ( i > 0 && mask < 0 ) || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      spell_callbacks[ i ].push_back( cb );
      heal_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_tick_callback =========================================

void player_t::register_tick_callback( int64_t mask,
                                       action_callback_t* cb )
{
  for ( int64_t i=0; i < RESULT_MAX; i++ )
  {
    if ( ( i > 0 && mask < 0 ) || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      tick_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_heal_callback ========================================

void player_t::register_heal_callback( int64_t mask,
                                        action_callback_t* cb )
{
  for ( int64_t i=0; i < RESULT_MAX; i++ )
  {
    if ( ( i > 0 && mask < 0 ) || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      heal_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_harmful_spell_callback ========================================

void player_t::register_harmful_spell_callback( int64_t mask,
                                        action_callback_t* cb )
{
  for ( int64_t i=0; i < RESULT_MAX; i++ )
  {
    if ( ( i > 0 && mask < 0 ) || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      spell_callbacks[ i ].push_back( cb );
    }
  }
}


// player_t::register_tick_damage_callback ==================================

void player_t::register_tick_damage_callback( int64_t mask,
                                              action_callback_t* cb )
{
  for ( int64_t i=0; i < SCHOOL_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      tick_damage_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_direct_damage_callback ================================

void player_t::register_direct_damage_callback( int64_t mask,
                                                action_callback_t* cb )
{
  for ( int64_t i=0; i < SCHOOL_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      direct_damage_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_tick_heal_callback ==================================

void player_t::register_tick_heal_callback( int64_t mask,
                                            action_callback_t* cb )
{
  for ( int64_t i=0; i < SCHOOL_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      tick_heal_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::register_direct_heal_callback ================================

void player_t::register_direct_heal_callback( int64_t mask,
                                              action_callback_t* cb )
{
  for ( int64_t i=0; i < SCHOOL_MAX; i++ )
  {
    if ( mask < 0 || ( mask & ( int64_t( 1 ) << i ) ) )
    {
      direct_heal_callbacks[ i ].push_back( cb );
    }
  }
}

// player_t::recalculate_haste ==============================================

void player_t::recalculate_haste()
{
  spell_haste = 1.0 / ( 1.0 + haste_rating / rating. spell_haste );
  attack_haste = 1.0 / ( 1.0 + haste_rating / rating.attack_haste );
}

// player_t::recent_cast ====================================================

bool player_t::recent_cast()
{
  return ( last_cast > 0 ) && ( ( last_cast + 5.0 ) > sim -> current_time );
}

// player_t::find_action ====================================================

action_t* player_t::find_action( const std::string& str )
{
  for ( action_t* a = action_list; a; a = a -> next )
    if ( str == a -> name_str )
      return a;

  return 0;
}

// player_t::aura_gain ======================================================

void player_t::aura_gain( const char* aura_name , double value )
{
  if ( sim -> log && ! sleeping )
  {
    log_t::output( sim, "%s gains %s ( value=%.2f )", name(), aura_name, value );
  }

}

// player_t::aura_loss ======================================================

void player_t::aura_loss( const char* aura_name , double value )
{
  if ( sim -> log && ! sleeping )
  {
    log_t::output( sim, "%s loses %s", name(), aura_name );
  }
}

// player_t::find_cooldown ==================================================

cooldown_t* player_t::find_cooldown( const std::string& name )
{
  for ( cooldown_t* c = cooldown_list; c; c = c -> next )
  {
    if ( c -> name_str == name )
      return c;
  }

  return 0;
}

// player_t::find_dot =======================================================

dot_t* player_t::find_dot( const std::string& name )
{
  for ( dot_t* d = dot_list; d; d = d -> next )
  {
    if ( d -> name_str == name )
      return d;
  }

  return 0;
}

// player_t::get_cooldown ===================================================

cooldown_t* player_t::get_cooldown( const std::string& name )
{
  cooldown_t* c=0;

  for ( c = cooldown_list; c; c = c -> next )
  {
    if ( c -> name_str == name )
      return c;
  }

  c = new cooldown_t( name, this );

  cooldown_t** tail = &cooldown_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  c -> next = *tail;
  *tail = c;

  return c;
}

// player_t::get_dot ========================================================

dot_t* player_t::get_dot( const std::string& name )
{
  dot_t* d=0;

  for ( d = dot_list; d; d = d -> next )
  {
    if ( d -> name_str == name )
      return d;
  }

  d = new dot_t( name, this );

  dot_t** tail = &dot_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  d -> next = *tail;
  *tail = d;

  return d;
}

// player_t::get_gain =======================================================

gain_t* player_t::get_gain( const std::string& name )
{
  gain_t* g=0;

  for ( g = gain_list; g; g = g -> next )
  {
    if ( g -> name_str == name )
      return g;
  }

  g = new gain_t( name );

  gain_t** tail = &gain_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  g -> next = *tail;
  *tail = g;

  return g;
}

// player_t::get_proc =======================================================

proc_t* player_t::get_proc( const std::string& name )
{
  proc_t* p=0;

  for ( p = proc_list; p; p = p -> next )
  {
    if ( p -> name_str == name )
      return p;
  }

  p = new proc_t( sim, name );

  proc_t** tail = &proc_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  p -> next = *tail;
  *tail = p;

  p -> player = this;

  return p;
}

// player_t::get_stats ======================================================

stats_t* player_t::get_stats( const std::string& n, action_t* a )
{
  stats_t* stats=0;

  for ( stats = stats_list; stats; stats = stats -> next )
  {
    if ( stats -> name_str == n )
    {
      if ( a ) stats -> action_list.push_back( a );
      return stats;
    }
  }

  if ( ! stats )
  {
    stats = new stats_t( n, this );
    stats -> init();
    if ( a ) stats -> action_list.push_back( a );
    stats_t** tail= &stats_list;
    while ( *tail && n > ( ( *tail ) -> name_str ) )
    {
      tail = &( ( *tail ) -> next );
    }
    stats -> next = *tail;
    *tail = stats;
  }

  stats -> player = this;

  return stats;
}

// player_t::get_uptime =====================================================

uptime_t* player_t::get_uptime( const std::string& name )
{
  uptime_t* u=0;

  for ( u = uptime_list; u; u = u -> next )
  {
    if ( u -> name_str == name )
      return u;
  }

  u = new uptime_t( name );

  uptime_t** tail = &uptime_list;

  while ( *tail && name > ( ( *tail ) -> name_str ) )
  {
    tail = &( ( *tail ) -> next );
  }

  u -> next = *tail;
  *tail = u;

  return u;
}

// player_t::get_rng =======================================================

rng_t* player_t::get_rng( const std::string& n, int type )
{
  assert( sim -> rng );

  if ( type == RNG_GLOBAL ) return sim -> rng;
  if ( type == RNG_DETERMINISTIC ) return sim -> deterministic_rng;

  if ( ! sim -> smooth_rng ) return ( sim -> deterministic_roll ? sim -> deterministic_rng : sim -> rng );

  rng_t* rng=0;

  for ( rng = rng_list; rng; rng = rng -> next )
  {
    if ( rng -> name_str == n )
      return rng;
  }

  if ( ! rng )
  {
    rng = rng_t::create( sim, n, type );
    rng -> next = rng_list;
    rng_list = rng;
  }

  return rng;
}

// player_t::get_player_distance ===========================================

double player_t::get_player_distance( player_t* p )
{
  // Euclidean Distance *Squared* as sqrt() is slow
  double distance = 0;

  distance = ( ( p -> x_position - this -> x_position ) * ( p -> x_position - this -> x_position ) + ( p -> y_position - this -> y_position ) * ( p -> y_position - this -> y_position ) );

  return distance;
}

// player_t::get_position_distance =========================================

double player_t::get_position_distance( double m, double v )
{
  // Euclidean Distance *Squared* as sqrt() is slow
  double distance = 0;

  distance = ( ( this -> x_position - m ) * ( this -> x_position - m ) + ( this -> y_position - v ) * ( this -> y_position - v ) );

  return distance;
}

// player_t::debuffs_t::snared ===============================================

bool player_t::debuffs_t::snared()
{
  if ( infected_wounds -> check() ) return true;
  if ( judgements_of_the_just -> check() ) return true;
  if ( slow -> check() ) return true;
  if ( thunder_clap -> check() ) return true;
  return false;
}

// Chosen Movement Actions

struct start_moving_t : public action_t
{
  start_moving_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "start_moving", player )
  {
    parse_options( NULL, options_str );
    trigger_gcd = 0;
    cooldown -> duration = 0.5;
    harmful = false;
  }

  virtual void execute()
  {
    player -> buffs.self_movement -> trigger();

    if ( sim -> log ) log_t::output( sim, "%s starts moving.", player -> name() );
    update_ready();
  }

  virtual bool ready()
  {
    if ( player -> buffs.self_movement -> check() )
      return false;

    return action_t::ready();
  }
};

struct stop_moving_t : public action_t
{
  stop_moving_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "stop_moving", player )
  {
    parse_options( NULL, options_str );
    trigger_gcd = 0;
    cooldown -> duration = 0.5;
    harmful = false;
  }

  virtual void execute()
  {
    player -> buffs.self_movement -> expire();

    if ( sim -> log ) log_t::output( sim, "%s stops moving.", player -> name() );
    update_ready();
  }

  virtual bool ready()
  {
    if ( ! player -> buffs.self_movement -> check() )
      return false;

    return action_t::ready();
  }
};

// ===== Racial Abilities ==================================================

// Arcane Torrent ==========================================================

struct arcane_torrent_t : public action_t
{
  arcane_torrent_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "arcane_torrent", player )
  {
    parse_options( NULL, options_str );
    trigger_gcd = 0;
    cooldown -> duration = 120;
  }

  virtual void execute()
  {
    int resource = player -> primary_resource();
    double gain = 0;
    switch( resource )
    {
    case RESOURCE_MANA:
      gain = player -> resource_max [ RESOURCE_MANA ] * 0.06;
      break;
    case RESOURCE_ENERGY:
    case RESOURCE_FOCUS:
    case RESOURCE_RAGE:
    case RESOURCE_RUNIC:
      gain = 15;
      break;
    default:
      break;
    }

    if( gain > 0 )
      player -> resource_gain( resource, gain, player -> gains.arcane_torrent );

    update_ready();
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_BLOOD_ELF )
      return false;

    int resource = player -> primary_resource();
    switch( resource )
    {
    case RESOURCE_MANA:
      if( player -> resource_current [ resource ] / player -> resource_max [ resource ] <= 0.94 )
        return true;
      break;
    case RESOURCE_ENERGY:
    case RESOURCE_FOCUS:
    case RESOURCE_RAGE:
    case RESOURCE_RUNIC:
      if( player -> resource_max [ resource ] - player -> resource_current [ resource ] >= 15 )
        return true;
      break;
    default:
      break;
    }

    return false;
  }
};

// Berserking ==========================================================

struct berserking_t : public action_t
{
  berserking_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "berserking", player )
  {
    parse_options( NULL, options_str );
    trigger_gcd = 0;
    cooldown -> duration = 180;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    update_ready();

    player -> buffs.berserking -> trigger();
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_TROLL )
      return false;

    return true;
  }
};

// Blood Fury ==========================================================

struct blood_fury_t : public action_t
{
  blood_fury_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "blood_fury", player )
  {
    parse_options( NULL, options_str );
    trigger_gcd = 0;
    cooldown -> duration = 120;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    update_ready();

    if ( player -> type == WARRIOR || player -> type == ROGUE || player -> type == DEATH_KNIGHT ||
         player -> type == HUNTER  || player -> type == SHAMAN )
    {
      player -> buffs.blood_fury_ap -> trigger();
    }

    if ( player -> type == SHAMAN  || player -> type == WARLOCK || player -> type == MAGE )
    {
      player -> buffs.blood_fury_sp -> trigger();
    }
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_ORC )
      return false;

    return true;
  }
};

// Stoneform ==========================================================

struct stoneform_t : public action_t
{
  stoneform_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "stoneform", player )
  {
    parse_options( NULL, options_str );
    trigger_gcd = 0;
    cooldown -> duration = 120;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    update_ready();

    player -> buffs.stoneform -> trigger();
  }

  virtual bool ready()
  {
    if( ! action_t::ready() )
      return false;

    if ( player -> race != RACE_DWARF )
      return false;

    return true;
  }
};

// Cycle Action ============================================================

struct cycle_t : public action_t
{
  action_t* current_action;

  cycle_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "cycle", player ), current_action( 0 )
  {}

  virtual void reset()
  {
    action_t::reset();

    if ( ! current_action )
    {
      current_action = next;
      if ( ! current_action )
      {
        sim -> errorf( "Player %s has no actions after 'cycle'\n", player -> name() );
        sim -> cancel();
      }
      for ( action_t* a = next; a; a = a -> next ) a -> background = true;
    }
  }

  virtual void schedule_execute()
  {
    player -> last_foreground_action = current_action;
    current_action -> schedule_execute();
    current_action = current_action -> next;
    if ( ! current_action ) current_action = next;
  }

  virtual bool ready()
  {
    if ( ! current_action ) return false;

    return current_action -> ready();
  }
};

// Lifeblood ===============================================================

struct lifeblood_t : public action_t
{
  lifeblood_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "lifeblood", player )
  {
    parse_options( NULL, options_str );
    harmful = false;
    trigger_gcd = 0;
    cooldown -> duration = 120;
  }

  void lockout( double duration )
  {
    if( duration <= 0 ) return;
    double ready = sim -> current_time + duration;
    for( action_t* a = player -> action_list; a; a = a -> next )
    {
      if( a -> name_str == "use_item" || a -> name_str == "lifeblood" )
      {
        if( ready > a -> cooldown -> ready )
        {
          a -> cooldown -> ready = ready;
        }
      }
    }
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), name() );

    update_ready();

    player -> buffs.lifeblood -> trigger();
    lockout( player -> buffs.lifeblood -> remains() );
  }

  virtual bool ready()
  {
    if ( player -> profession[ PROF_HERBALISM ] < 450 )
      return false;

    return action_t::ready();
  }
};

// Restart Sequence Action =================================================

struct restart_sequence_t : public action_t
{
  std::string seq_name_str;

  restart_sequence_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "restart_sequence", player )
  {
    seq_name_str = "default"; // matches default name for sequences
    option_t options[] =
    {
      { "name", OPT_STRING, &seq_name_str },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    trigger_gcd = 0;
  }

  virtual void execute()
  {
    for ( action_t* a = player -> action_list; a; a = a -> next )
    {
      if ( a -> type != ACTION_SEQUENCE )
        continue;

      if ( ! seq_name_str.empty() )
        if ( seq_name_str != a -> name_str )
          continue;

      ( ( sequence_t* ) a ) -> restart();
    }
  }
};

// Restore Mana Action =====================================================

struct restore_mana_t : public action_t
{
  double mana;

  restore_mana_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "restore_mana", player ), mana( 0 )
  {
    option_t options[] =
    {
      { "mana", OPT_FLT, &mana },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    trigger_gcd = 0;
  }

  virtual void execute()
  {
    double mana_missing = player -> resource_max[ RESOURCE_MANA ] - player -> resource_current[ RESOURCE_MANA ];
    double mana_gain = mana;

    if ( mana_gain == 0 || mana_gain > mana_missing ) mana_gain = mana_missing;

    if ( mana_gain > 0 )
    {
      player -> resource_gain( RESOURCE_MANA, mana_gain, player -> gains.restore_mana );
    }
  }
};

// Snapshot Stats ============================================================

struct snapshot_stats_t : public action_t
{
  attack_t* attack;
  spell_t* spell;

  snapshot_stats_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "snapshot_stats", player ), attack( 0 ), spell( 0 )
  {
    parse_options( NULL, options_str );
    base_execute_time = 0.001; // Needs to be non-zero to ensure all the buffs have been setup.
    trigger_gcd = 0;
  }

  virtual void execute()
  {
    player_t* p = player;

    if ( sim -> log ) log_t::output( sim, "%s performs %s", p -> name(), name() );

    p -> buffed_spell_haste  = p -> composite_spell_haste();
    p -> buffed_attack_haste = p -> composite_attack_haste();
    p -> buffed_mastery      = p -> composite_mastery();

    p -> attribute_buffed[ ATTR_STRENGTH  ] = floor( p -> strength()  );
    p -> attribute_buffed[ ATTR_AGILITY   ] = floor( p -> agility()   );
    p -> attribute_buffed[ ATTR_STAMINA   ] = floor( p -> stamina()   );
    p -> attribute_buffed[ ATTR_INTELLECT ] = floor( p -> intellect() );
    p -> attribute_buffed[ ATTR_SPIRIT    ] = floor( p -> spirit()    );

    for( int i=0; i < RESOURCE_MAX; i++ ) p -> resource_buffed[ i ] = p -> resource_max[ i ];

    p -> buffed_spell_power       = floor( p -> composite_spell_power( SCHOOL_MAX ) * p -> composite_spell_power_multiplier() );
    p -> buffed_spell_hit         = p -> composite_spell_hit();
    p -> buffed_spell_crit        = p -> composite_spell_crit();
    p -> buffed_spell_penetration = p -> composite_spell_penetration();
    p -> buffed_mp5               = p -> composite_mp5();

    p -> buffed_attack_power       = p -> composite_attack_power() * p -> composite_attack_power_multiplier();
    p -> buffed_attack_hit         = p -> composite_attack_hit();
    p -> buffed_attack_expertise   = p -> composite_attack_expertise();
    p -> buffed_attack_crit        = p -> composite_attack_crit();

    p -> buffed_armor       = p -> composite_armor();
    p -> buffed_miss        = p -> composite_tank_miss( SCHOOL_PHYSICAL );
    p -> buffed_dodge       = p -> composite_tank_dodge() - p -> diminished_dodge();
    p -> buffed_parry       = p -> composite_tank_parry() - p -> diminished_parry();
    p -> buffed_block       = p -> composite_tank_block();
    p -> buffed_crit        = p -> composite_tank_crit( SCHOOL_PHYSICAL );

    int role = p -> primary_role();
    int delta_level = sim -> target -> level - p -> level;
    double spell_hit_extra=0, attack_hit_extra=0, expertise_extra=0;

    if ( role == ROLE_SPELL || role == ROLE_HYBRID || role == ROLE_HEAL )
    {
      if ( ! spell ) spell = new spell_t( "snapshot_spell", p );
      spell -> background = true;
      spell -> player_buff();
      spell -> target_debuff( target, DMG_DIRECT );
      double chance = spell -> miss_chance( delta_level );
      if ( chance < 0 ) spell_hit_extra = -chance * p -> rating.spell_hit;
    }

    if ( role == ROLE_ATTACK || role == ROLE_HYBRID || role == ROLE_TANK )
    {
      if ( ! attack ) attack = new attack_t( "snapshot_attack", p );
      attack -> background = true;
      attack -> player_buff();
      attack -> target_debuff( target, DMG_DIRECT );
      double chance = attack -> miss_chance( delta_level );
      if( p -> dual_wield() ) chance += 0.19;
      if ( chance < 0 ) attack_hit_extra = -chance * p -> rating.attack_hit;
      chance = attack -> dodge_chance(  delta_level );
      if ( chance < 0 ) expertise_extra = -chance * 4 * p -> rating.expertise;
    }

    p -> over_cap[ STAT_HIT_RATING ] = std::max( spell_hit_extra, attack_hit_extra );
    p -> over_cap[ STAT_EXPERTISE_RATING ] = expertise_extra;

    p -> attribute_buffed[ ATTRIBUTE_NONE ] = 1;
  }

  virtual bool ready()
  {
    if ( sim -> current_iteration > 0 ) return false;
    if ( player -> attribute_buffed[ ATTRIBUTE_NONE ] != 0 ) return false;
    return action_t::ready();
  }
};

// Wait Until Ready Action ===================================================

struct wait_until_ready_t : public action_t
{
  double sec;

  wait_until_ready_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "wait", player ), sec( 1.0 )
  {
    option_t options[] =
    {
      { "sec", OPT_FLT, &sec },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    trigger_gcd = 0;
  }

  virtual double execute_time() SC_CONST
  {
    double wait = sec;
    double remains = 0;

    for ( action_t* a = player -> action_list; a; a = a -> next )
    {
      if ( a -> background ) continue;

      remains = a -> cooldown -> remains();
      if ( remains > 0 && remains < wait ) wait = remains;

      remains = a -> dot -> remains();
      if ( remains > 0 && remains < wait ) wait = remains;
    }

    return wait + 0.001;
  }

  virtual void execute()
  {
    player -> total_waiting += time_to_execute;
  }
};

// Wait Fixed Action =========================================================

struct wait_fixed_t : public action_t
{
  action_expr_t* time_expr;

  wait_fixed_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "wait", player )
  {
    std::string sec_str = "1.0";

    option_t options[] =
    {
      { "sec", OPT_STRING, &sec_str },
      { NULL,  OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    time_expr = action_expr_t::parse( this, sec_str );
    trigger_gcd = 0;
  }

  virtual double execute_time() SC_CONST
  {
    int result = time_expr -> evaluate();
    assert( result == TOK_NUM );
    return time_expr -> result_num;
  }

  virtual void execute()
  {
    player -> total_waiting += time_to_execute;
  }
};

// Use Item Action ===========================================================

struct use_item_t : public action_t
{
  item_t* item;
  spell_t* discharge;
  action_callback_t* trigger;
  stat_buff_t* buff;
  std::string use_name;

  use_item_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "use_item", player ),
    item( 0 ), discharge( 0 ), trigger( 0 ), buff( 0 )
  {
    std::string item_name;
    option_t options[] =
    {
      { "name", OPT_STRING, &item_name },
      { NULL, OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    if ( item_name.empty() )
    {
      sim -> errorf( "Player %s has 'use_item' action with no 'name=' option.\n", player -> name() );
      return;
    }

    item = player -> find_item( item_name );
    if ( ! item )
    {
      sim -> errorf( "Player %s attempting 'use_item' action with item '%s' which is not currently equipped.\n", player -> name(), item_name.c_str() );
      return;
    }
    if ( ! item -> use.active() )
    {
      sim -> errorf( "Player %s attempting 'use_item' action with item '%s' which has no 'use=' encoding.\n", player -> name(), item_name.c_str() );
      item = 0;
      return;
    }

    item_t::special_effect_t& e = item -> use;

    use_name = e.name_str.empty() ? item_name : e.name_str;

    if ( e.trigger_type )
    {
      if ( e.cost_reduction && e.school && e.discharge_amount )
      {
        trigger = unique_gear_t::register_cost_reduction_proc( e.trigger_type, e.trigger_mask, use_name, player,
                                                     e.school, e.max_stacks, e.discharge_amount,
                                                     e.proc_chance, 0.0/*dur*/, 0.0/*cd*/, false, e.reverse, 0 );
      }
      else if ( e.stat )
      {
        trigger = unique_gear_t::register_stat_proc( e.trigger_type, e.trigger_mask, use_name, player,
                                                     e.stat, e.max_stacks, e.stat_amount,
                                                     e.proc_chance, 0.0/*dur*/, 0.0/*cd*/, e.tick, e.reverse, 0 );
      }
      else if ( e.school )
      {
        trigger = unique_gear_t::register_discharge_proc( e.trigger_type, e.trigger_mask, use_name, player,
                                                          e.max_stacks, e.school, e.discharge_amount, e.discharge_scaling,
                                                          e.proc_chance, 0.0/*cd*/, e.no_crit, e.no_player_benefits, e.no_debuffs );
      }

      if ( trigger ) trigger -> deactivate();
    }
    else if ( e.school )
    {
      struct discharge_spell_t : public spell_t
      {
        discharge_spell_t( const char* n, player_t* p, double a, const school_type s ) :
          spell_t( n, p, RESOURCE_NONE, s )
        {
          trigger_gcd = 0;
          base_dd_min = a;
          base_dd_max = a;
          may_crit    = true;
          background  = true;
          base_spell_power_multiplier = 0;
          init();
        }
      };

      discharge = new discharge_spell_t( use_name.c_str(), player, e.discharge_amount, e.school );
    }
    else if ( e.stat )
    {
      if( e.max_stacks  == 0 ) e.max_stacks  = 1;
      if( e.proc_chance == 0 ) e.proc_chance = 1;

      buff = new stat_buff_t( player, use_name, e.stat, e.stat_amount, e.max_stacks, e.duration, 0, e.proc_chance, false, e.reverse );
    }
    else assert( false );

    std::string cooldown_name = use_name;
    cooldown_name += "_";
    cooldown_name += item -> slot_name();

    cooldown = player -> get_cooldown( cooldown_name );
    cooldown -> duration = item -> use.cooldown;
    trigger_gcd = 0;
  }

  void lockout( double duration )
  {
    if( duration <= 0 ) return;
    double ready = sim -> current_time + duration;
    for( action_t* a = player -> action_list; a; a = a -> next )
    {
      if( a -> name_str == "use_item" || a -> name_str == "lifeblood" )
      {
        if( ready > a -> cooldown -> ready )
        {
          a -> cooldown -> ready = ready;
        }
      }
    }
  }

  virtual void execute()
  {
    if ( discharge )
    {
      discharge -> execute();
    }
    else if ( trigger )
    {
      if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), use_name.c_str() );

      trigger -> activate();

      if ( item -> use.duration )
      {
        struct trigger_expiration_t : public event_t
        {
          item_t* item;
          action_callback_t* trigger;

          trigger_expiration_t( sim_t* sim, player_t* player, item_t* i, action_callback_t* t ) : event_t( sim, player ), item( i ), trigger( t )
          {
            name = item -> name();
            sim -> add_event( this, item -> use.duration );
          }
          virtual void execute()
          {
            trigger -> deactivate();
          }
        };

        new ( sim ) trigger_expiration_t( sim, player, item, trigger );

        lockout( item -> use.duration );
      }
    }
    else if( buff )
    {
      if ( sim -> log ) log_t::output( sim, "%s performs %s", player -> name(), use_name.c_str() );
      buff -> trigger();
      lockout( buff -> buff_duration );
    }
    else assert( false );

    update_ready();
  }

  virtual void reset()
  {
    action_t::reset();
    if ( trigger ) trigger -> deactivate();
  }

  virtual bool ready()
  {
    if( ! item ) return false;
    return action_t::ready();
  }
};

// Cancel Buff ================================================================

struct cancel_buff_t : public action_t
{
  buff_t* buff;

  cancel_buff_t( player_t* player, const std::string& options_str ) :
    action_t( ACTION_OTHER, "cancel_buff", player ), buff( 0 )
  {
    std::string buff_name;
    option_t options[] =
    {
      { "name", OPT_STRING, &buff_name },
      { NULL,  OPT_UNKNOWN, NULL }
    };
    parse_options( options, options_str );

    if( buff_name.empty() )
    {
      sim -> errorf( "Player %s uses cancel_buff without specifying the name of the buff\n", player -> name() );
      sim -> cancel();
    }

    buff = buff_t::find( player, buff_name );

    if( ! buff )
    {
      sim -> errorf( "Player %s uses cancel_buff with unknown buff %s\n", player -> name(), buff_name.c_str() );
      sim -> cancel();
    }
    trigger_gcd = 0;
  }

  virtual void execute()
  {
    if ( sim -> log ) log_t::output( sim, "%s cancels buff %s", player -> name(), buff -> name() );
    buff -> expire();
  }

  virtual bool ready()
  {
    if( ! buff || ! buff -> check() )
      return false;
    return action_t::ready();
  }
};

// player_t::create_action ==================================================

action_t* player_t::create_action( const std::string& name,
                                   const std::string& options_str )
{
  if ( name == "arcane_torrent"   ) return new   arcane_torrent_t( this, options_str );
  if ( name == "berserking"       ) return new       berserking_t( this, options_str );
  if ( name == "blood_fury"       ) return new       blood_fury_t( this, options_str );
  if ( name == "cancel_buff"      ) return new      cancel_buff_t( this, options_str );
  if ( name == "cycle"            ) return new            cycle_t( this, options_str );
  if ( name == "lifeblood"        ) return new        lifeblood_t( this, options_str );
  if ( name == "restart_sequence" ) return new restart_sequence_t( this, options_str );
  if ( name == "restore_mana"     ) return new     restore_mana_t( this, options_str );
  if ( name == "sequence"         ) return new         sequence_t( this, options_str );
  if ( name == "snapshot_stats"   ) return new   snapshot_stats_t( this, options_str );
  if ( name == "start_moving"     ) return new     start_moving_t( this, options_str );
  if ( name == "stoneform"        ) return new        stoneform_t( this, options_str );
  if ( name == "stop_moving"      ) return new      stop_moving_t( this, options_str );
  if ( name == "use_item"         ) return new         use_item_t( this, options_str );
  if ( name == "wait"             ) return new       wait_fixed_t( this, options_str );
  if ( name == "wait_until_ready" ) return new wait_until_ready_t( this, options_str );

  return consumable_t::create_action( this, name, options_str );
}

// player_t::find_pet =======================================================

pet_t* player_t::find_pet( const std::string& pet_name )
{
  for ( pet_t* p = pet_list; p; p = p -> next_pet )
    if ( p -> name_str == pet_name )
      return p;

  return 0;
}

// player_t::trigger_replenishment ================================================

void player_t::trigger_replenishment()
{
  if ( sim -> overrides.replenishment )
    return;

  if ( sim -> replenishment_targets > 0 )
  {
    choose_replenish_targets( this );
  }
  else
  {
    replenish_raid( this );
  }
}

// player_t::parse_talent_trees ===================================================

bool player_t::parse_talent_trees( int encoding[], const uint32_t size )
{
  int index=0;

  for( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    int tree_size = talent_trees[ i ].size();

    for( int j=0; j < tree_size; j++ )
    {
      talent_trees[ i ][ j ] -> set_rank( encoding[ index++ ] );
    }
  }

  return true;
}

// player_t::parse_talents_armory ===========================================

bool player_t::parse_talents_armory( const std::string& talent_string )
{
  int encoding[MAX_TALENT_SLOTS];

  for( int i=0; i < MAX_TALENT_SLOTS; i++ ) encoding[ i ] = 0;

  for( unsigned int i = 0; i < talent_string.size(); i++ )
  {
    char c = talent_string[ i ];
    if ( c < '0' || c > '5' )
    {
      sim -> errorf( "Player %s has illegal character '%c' in talent encoding.\n", name(), c );
      return false;
    }
    encoding[i] = c - '0';
  }

  return parse_talent_trees( encoding, talent_string.size() );
}

// player_t::parse_talents_wowhead ==========================================

bool player_t::parse_talents_wowhead( const std::string& talent_string )
{
  // wowhead format: [tree_1]Z[tree_2]Z[tree_3] where the trees are character encodings
  // each character expands to a pair of numbers [0-5][0-5]
  // unused deeper talents are simply left blank instead of filling up the string with zero-zero encodings

  struct decode_t
  {
    char key, first, second;
  }
  decoding[] =
  {
    { '0', '0', '0' },  { 'z', '0', '1' },  { 'M', '0', '2' },  { 'c', '0', '3' }, { 'm', '0', '4' },  { 'V', '0', '5' },
    { 'o', '1', '0' },  { 'k', '1', '1' },  { 'R', '1', '2' },  { 's', '1', '3' }, { 'a', '1', '4' },  { 'q', '1', '5' },
    { 'b', '2', '0' },  { 'd', '2', '1' },  { 'r', '2', '2' },  { 'f', '2', '3' }, { 'w', '2', '4' },  { 'i', '2', '5' },
    { 'h', '3', '0' },  { 'u', '3', '1' },  { 'G', '3', '2' },  { 'I', '3', '3' }, { 'N', '3', '4' },  { 'A', '3', '5' },
    { 'L', '4', '0' },  { 'p', '4', '1' },  { 'T', '4', '2' },  { 'j', '4', '3' }, { 'n', '4', '4' },  { 'y', '4', '5' },
    { 'x', '5', '0' },  { 't', '5', '1' },  { 'g', '5', '2' },  { 'e', '5', '3' }, { 'v', '5', '4' },  { 'E', '5', '5' },
    { '\0', '\0', '\0' }
  };

  int encoding[ MAX_TALENT_SLOTS ];
  unsigned tree_count[ MAX_TALENT_TREES ];

  for( int i=0; i < MAX_TALENT_SLOTS; i++ ) encoding[ i ] = 0;
  for( int i=0; i < MAX_TALENT_TREES; i++ ) tree_count[ i ] = 0;

  int tree = 0;
  int count = 0;

  for ( unsigned int i=1; i < talent_string.length(); i++ )
  {
    if ( tree >= MAX_TALENT_TREES )
    {
      sim -> errorf( "Player %s has malformed wowhead talent string. Too many talent trees specified.\n", name() );
      return false;
    }

    char c = talent_string[ i ];

    if ( c == ':' ) break; // glyph encoding follows

    if ( c == 'Z' )
    {
      count = 0;
      for( int j=0; j <= tree; j++ )
        count += talent_trees[ j ].size();
      tree++;
      continue;
    }

    decode_t* decode = 0;
    for ( int j=0; decoding[ j ].key != '\0' && ! decode; j++ )
    {
      if ( decoding[ j ].key == c ) decode = decoding + j;
    }

    if ( ! decode )
    {
      sim -> errorf( "Player %s has malformed wowhead talent string. Translation for '%c' unknown.\n", name(), c );
      return false;
    }

    encoding[ count++ ] += decode -> first - '0';
    tree_count[ tree ] += 1;

    if ( tree_count[ tree ] < talent_trees[ tree ].size() )
    {
      encoding[ count++ ] += decode -> second - '0';
      tree_count[ tree ] += 1;
    }

    if ( tree_count[ tree ] >= talent_trees[ tree ].size() )
    {
      tree++;
    }
  }

  if ( sim -> debug )
  {
    std::string str_out = "";
    for(  int i=0; i < count; i++ ) str_out += ( char )encoding[i];
    util_t::fprintf( sim -> output_file, "%s Wowhead talent string translation: %s\n", name(), str_out.c_str() );
  }

  return parse_talent_trees( encoding, count );
}

// player_t::create_talents =================================================

struct compare_talents
{
  bool operator()( const talent_t* left, const talent_t* right ) SC_CONST
  {
    const talent_data_t* l = left  -> t_data;
    const talent_data_t* r = right -> t_data;

    if( l -> tab_page() == r -> tab_page() )
    {
      if( l -> row() == r -> row() )
      {
        if( l -> col() == r -> col() )
        {
          return ( l -> id() > r -> id() ); // not a typo: Dive comes before Dash in pet talent string!
        }
        return ( l -> col() < r -> col() );
      }
      return ( l -> row() < r -> row() );
    }
    return ( l -> tab_page() < r -> tab_page() );
  }
};

void player_t::create_talents()
{
  int cid_mask = util_t::class_id_mask( type );
  talent_data_t* talent_data = talent_data_t::list( ptr );

  for( int i=0; talent_data[ i ].name_cstr(); i++ )
  {
    talent_data_t& td = talent_data[ i ];

    if( cid_mask )
    {
      if( cid_mask & td.mask_class() )
      {
        talent_t* t = new talent_t( this, &td );
        talent_trees[ td.tab_page() ].push_back( t );
        option_t::add( options, t -> s_token.c_str(), OPT_TALENT_RANK, ( void* ) t );
      }
    }
    else if( td.mask_pet() )
    {
      for( int j=0; j < MAX_TALENT_TREES; j++ )
      {
        if( td.mask_pet() & ( 1 << j ) )
        {
          talent_t* t = new talent_t( this, &td );
          talent_trees[ j ].push_back( t );
          option_t::add( options, t -> s_token.c_str(), OPT_TALENT_RANK, ( void* ) t );
        }
      }
    }
  }

  for( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    std::vector<talent_t*>& tree = talent_trees[ i ];
    if( ! tree.empty() ) std::sort( tree.begin(), tree.end(), compare_talents() );
  }
}

// player_t::find_talent ====================================================

talent_t* player_t::find_talent( const std::string& n,
                                 int tree )
{
  for( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    if( tree != TALENT_TAB_NONE && tree != i )
      continue;

    for( int j=talent_trees[ i ].size()-1; j >= 0; j-- )
    {
      talent_t* t = talent_trees[ i ][ j ];

      if( n == t -> td -> name_cstr() )
      {
        return t;
      }
    }
  }

  sim -> errorf( "Player %s unable to find talent %s\n", name(), n.c_str() );
//sim -> cancel();
  return 0;
}

// player_t::create_glyphs ==================================================

void player_t::create_glyphs()
{
  std::vector<unsigned> glyph_ids;
  int num_glyphs = dbc_t::glyphs( glyph_ids, util_t::class_id( type ), ptr );

  for( int i=0; i < num_glyphs; i++ )
  {
    glyphs.push_back( new glyph_t( this, spell_data_t::find( glyph_ids[ i ], "", ptr ) ) );
  }
}

// player_t::find_glyph =====================================================

glyph_t* player_t::find_glyph( const std::string& n )
{
  for( int i=glyphs.size()-1; i >= 0; i-- )
  {
    glyph_t* g = glyphs[ i ];
    if( n == g -> sd -> name_cstr() ) return g;
    if( n == g -> s_token ) return g; // Armory-ized
  }

  sim -> errorf( "\nPlayer %s unable to find glyph %s\n", name(), n.c_str() );
//sim -> cancel();
  return 0;
}

// player_t::create_expression ==============================================

action_expr_t* player_t::create_expression( action_t* a,
                                            const std::string& name_str )
{
  int resource_type = util_t::parse_resource_type( name_str );
  if ( resource_type != RESOURCE_NONE )
  {
    struct resource_expr_t : public action_expr_t
    {
      int resource_type;
      resource_expr_t( action_t* a, const std::string& n, int r ) : action_expr_t( a, n, TOK_NUM ), resource_type( r ) {}
      virtual int evaluate() { result_num = action -> player -> resource_current[ resource_type ]; return TOK_NUM; }
    };
    return new resource_expr_t( a, name_str, resource_type );
  }
  if ( name_str == "level" )
  {
    struct level_expr_t : public action_expr_t
    {
      level_expr_t( action_t* a ) : action_expr_t( a, "level", TOK_NUM ) {}
      virtual int evaluate() { player_t* p = action -> player; result_num = p -> level; return TOK_NUM; }
    };
    return new level_expr_t( a );
  }
  if ( name_str == "multiplier" )
  {
    struct multiplier_expr_t : public action_expr_t
    {
      multiplier_expr_t( action_t* a ) : action_expr_t( a, "multiplier", TOK_NUM ) {}
      virtual int evaluate() { player_t* p = action -> player; result_num = p -> composite_player_multiplier( action -> school, action ); return TOK_NUM; }
    };
    return new multiplier_expr_t( a );
  }
  if ( name_str == "mana_pct" )
  {
    struct mana_pct_expr_t : public action_expr_t
    {
      mana_pct_expr_t( action_t* a ) : action_expr_t( a, "mana_pct", TOK_NUM ) {}
      virtual int evaluate() { player_t* p = action -> player; result_num = 100 * ( p -> resource_current[ RESOURCE_MANA ] / p -> resource_max[ RESOURCE_MANA ] ); return TOK_NUM; }
    };
    return new mana_pct_expr_t( a );
  }
  if ( name_str == "health_pct" )
  {
    struct health_pct_expr_t : public action_expr_t
    {
      player_t* player;
      health_pct_expr_t( action_t* a, player_t* p ) : action_expr_t( a, "health_pct", TOK_NUM ), player( p ) {}
      virtual int evaluate() { result_num = player -> health_percentage(); return TOK_NUM; }
    };
    return new health_pct_expr_t( a, this );
  }
  if ( name_str == "mana_deficit" )
  {
    struct mana_deficit_expr_t : public action_expr_t
    {
      mana_deficit_expr_t( action_t* a ) : action_expr_t( a, "mana_deficit", TOK_NUM ) {}
      virtual int evaluate() { player_t* p = action -> player; result_num = ( p -> resource_max[ RESOURCE_MANA ] - p -> resource_current[ RESOURCE_MANA ] ); return TOK_NUM; }
    };
    return new mana_deficit_expr_t( a );
  }
  if ( name_str == "in_combat" )
  {
    struct in_combat_expr_t : public action_expr_t
    {
      in_combat_expr_t( action_t* a ) : action_expr_t( a, "in_combat", TOK_NUM ) {}
      virtual int evaluate() { result_num = ( action -> player -> in_combat  ? 1 : 0 ); return TOK_NUM; }
    };
    return new in_combat_expr_t( a );
  }
  if ( name_str == "attack_haste" )
  {
    struct attack_haste_expr_t : public action_expr_t
    {
      attack_haste_expr_t( action_t* a ) : action_expr_t( a, "attack_haste", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> composite_attack_haste(); return TOK_NUM; }
    };
    return new attack_haste_expr_t( a );
  }
  if ( name_str == "attack_speed" )
  {
    struct attack_speed_expr_t : public action_expr_t
    {
      attack_speed_expr_t( action_t* a ) : action_expr_t( a, "attack_speed", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> composite_attack_speed(); return TOK_NUM; }
    };
    return new attack_speed_expr_t( a );
  }
  if ( name_str == "spell_haste" )
  {
    struct spell_haste_expr_t : public action_expr_t
    {
      spell_haste_expr_t( action_t* a ) : action_expr_t( a, "spell_haste", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> composite_spell_haste(); return TOK_NUM; }
    };
    return new spell_haste_expr_t( a );
  }
  if ( name_str == "energy_regen" )
  {
    struct energy_regen_expr_t : public action_expr_t
    {
      energy_regen_expr_t( action_t* a ) : action_expr_t( a, "energy_regen", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> energy_regen_per_second(); return TOK_NUM; }
    };
    return new energy_regen_expr_t( a );
  }
  if ( name_str == "focus_regen" )
  {
    struct focus_regen_expr_t : public action_expr_t
    {
      focus_regen_expr_t( action_t* a ) : action_expr_t( a, "focus_regen", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> focus_regen_per_second(); return TOK_NUM; }
    };
    return new focus_regen_expr_t( a );
  }
  if ( name_str == "time_to_die" )
  {
    struct time_to_die_expr_t : public action_expr_t
    {
      player_t* player;
      time_to_die_expr_t( action_t* a, player_t* p ) :
        action_expr_t( a, "target_time_to_die", TOK_NUM ), player( p ) {}
      virtual int evaluate() { result_num = player -> time_to_die();  return TOK_NUM; }
    };
    return new time_to_die_expr_t( a, this );
  }
  if ( name_str == "time_to_max_energy" )
  {
    struct time_to_max_energy_expr_t : public action_expr_t
    {
      time_to_max_energy_expr_t( action_t* a ) : action_expr_t( a, "time_to_max_energy", TOK_NUM ) {}
      virtual int evaluate()
      {
        result_num = ( action -> player -> resource_max[ RESOURCE_ENERGY ] -
                       action -> player -> resource_current[ RESOURCE_ENERGY ] ) /
                     action -> player -> energy_regen_per_second(); return TOK_NUM;
      }
    };
    return new time_to_max_energy_expr_t( a );
  }
  if ( name_str == "time_to_max_focus" )
  {
    struct time_to_max_focus_expr_t : public action_expr_t
    {
      time_to_max_focus_expr_t( action_t* a ) : action_expr_t( a, "time_to_max_focus", TOK_NUM ) {}
      virtual int evaluate()
      {
        result_num = ( action -> player -> resource_max[ RESOURCE_FOCUS ] -
                       action -> player -> resource_current[ RESOURCE_FOCUS ] ) /
                     action -> player -> focus_regen_per_second(); return TOK_NUM;
      }
    };
    return new time_to_max_focus_expr_t( a );
  }
  if ( name_str == "max_energy" )
  {
    struct max_energy_expr_t : public action_expr_t
    {
      max_energy_expr_t( action_t* a ) : action_expr_t( a, "max_energy", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> resource_max[ RESOURCE_ENERGY ]; return TOK_NUM; }
    };
    return new max_energy_expr_t( a );
  }
  if ( name_str == "max_focus" )
  {
    struct max_focus_expr_t : public action_expr_t
    {
      max_focus_expr_t( action_t* a ) : action_expr_t( a, "max_focus", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> resource_max[ RESOURCE_FOCUS ]; return TOK_NUM; }
    };
    return new max_focus_expr_t( a );
  }
  if ( name_str == "max_rage" )
  {
    struct max_rage_expr_t : public action_expr_t
    {
      max_rage_expr_t( action_t* a ) : action_expr_t( a, "max_rage", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> resource_max[ RESOURCE_RAGE ]; return TOK_NUM; }
    };
    return new max_rage_expr_t( a );
  }
  if ( name_str == "max_runic" )
  {
    struct max_runic_expr_t : public action_expr_t
    {
      max_runic_expr_t( action_t* a ) : action_expr_t( a, "max_runic", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> resource_max[ RESOURCE_RUNIC ]; return TOK_NUM; }
    };
    return new max_runic_expr_t( a );
  }
  if ( name_str == "max_mana" )
  {
    struct max_mana_expr_t : public action_expr_t
    {
      max_mana_expr_t( action_t* a ) : action_expr_t( a, "max_mana", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> resource_max[ RESOURCE_MANA ]; return TOK_NUM; }
    };
    return new max_mana_expr_t( a );
  }
  if ( name_str == "ptr" )
  {
    struct ptr_expr_t : public action_expr_t
    {
      ptr_expr_t( action_t* a ) : action_expr_t( a, "ptr", TOK_NUM ) {}
      virtual int evaluate() { result_num = action -> player -> dbc.ptr ? 1 : 0; return TOK_NUM; }
    };
    return new ptr_expr_t( a );
  }
  if ( name_str == "big_hitbox" )
  {
    struct big_hitbox_expr_t : public action_expr_t
    {
      big_hitbox_expr_t( action_t* a ) : action_expr_t( a, "big_hitbox", TOK_NUM ) {}
      virtual int evaluate() { result_num = ( action -> sim -> target -> big_hitbox  ? 1 : 0 ); return TOK_NUM; }
    };
    return new big_hitbox_expr_t( a );
  }
  std::vector<std::string> splits;
  int num_splits = util_t::string_split( splits, name_str, "." );
  if ( splits[ 0 ] == "pet" )
  {
    pet_t* pet = find_pet( splits[ 1 ] );
    if ( pet )
    {
      if ( splits[ 2 ] == "active" )
      {
        struct pet_active_expr_t : public action_expr_t
        {
          pet_t* pet;
          pet_active_expr_t( action_t* a, pet_t* p ) : action_expr_t( a, "pet_active", TOK_NUM ), pet( p ) {}
          virtual int evaluate() { result_num = ( pet -> sleeping ) ? 0 : 1; return TOK_NUM; }
        };
        return new pet_active_expr_t( a, pet );
      }
      else
      {
        return pet -> create_expression( a, name_str.substr( splits[ 1 ].length() + 5 ) );
      }
    }
  }
  else if ( splits[ 0 ] == "owner" )
  {
    if ( is_pet() )
    {
      pet_t* pet = ( pet_t* ) this;

      if ( pet-> owner )
      {
        return pet -> owner -> create_expression( a, name_str.substr( 6 ) );
      }
    }
  }
  else if ( num_splits == 3 )
  {
    if ( splits[ 0 ] == "buff" )
    {
      buff_t* buff = buff_t::find( this, splits[ 1 ] );
      if ( ! buff ) buff = buff_t::find( sim, splits[ 1 ] );
      if ( ! buff ) return 0;
      return buff -> create_expression( a, splits[ 2 ] );
    }
    else if ( splits[ 0 ] == "cooldown" )
    {
      cooldown_t* cooldown = get_cooldown( splits[ 1 ] );
      if ( splits[ 2 ] == "remains" )
      {
        struct cooldown_remains_expr_t : public action_expr_t
        {
          cooldown_t* cooldown;
          cooldown_remains_expr_t( action_t* a, cooldown_t* c ) : action_expr_t( a, "cooldown_remains", TOK_NUM ), cooldown( c ) {}
          virtual int evaluate() { result_num = cooldown -> remains(); return TOK_NUM; }
        };
        return new cooldown_remains_expr_t( a, cooldown );
      }
    }
    else if ( splits[ 0 ] == "dot" )
    {
      dot_t* dot = get_dot( splits[ 1 ] );
      if ( splits[ 2 ] == "duration" )
      {
        struct duration_expr_t : public action_expr_t
        {
          duration_expr_t( action_t* a ) : action_expr_t( a, "dot_duration", TOK_NUM ) {}
          virtual int evaluate() { result_num = action -> num_ticks * action -> tick_time(); return TOK_NUM; }
        };
        return new duration_expr_t( a );
      }
      if ( splits[ 2 ] == "multiplier" )
      {
        struct multiplier_expr_t : public action_expr_t
        {
          multiplier_expr_t( action_t* a ) : action_expr_t( a, "dot_multiplier", TOK_NUM ) {}
          virtual int evaluate() { result_num = action -> player_multiplier; return TOK_NUM; }
        };
        return new multiplier_expr_t( a );
      }
      if ( splits[ 2 ] == "remains" )
      {
        struct dot_remains_expr_t : public action_expr_t
        {
          dot_t* dot;
          dot_remains_expr_t( action_t* a, dot_t* d ) : action_expr_t( a, "dot_remains", TOK_NUM ), dot( d ) {}
          virtual int evaluate() { result_num = dot -> remains(); return TOK_NUM; }
        };
        return new dot_remains_expr_t( a, dot );
      }
      if ( splits[ 2 ] == "remains" )
      {
        struct dot_remains_expr_t : public action_expr_t
        {
          dot_t* dot;
          dot_remains_expr_t( action_t* a, dot_t* d ) : action_expr_t( a, "dot_remains", TOK_NUM ), dot( d ) {}
          virtual int evaluate() { result_num = dot -> remains(); return TOK_NUM; }
        };
        return new dot_remains_expr_t( a, dot );
      }
      if ( splits[ 2 ] == "ticks_remain" )
      {
        struct dot_ticks_remain_expr_t : public action_expr_t
        {
          dot_t* dot;
          dot_ticks_remain_expr_t( action_t* a, dot_t* d ) : action_expr_t( a, "dot_ticks_remain", TOK_NUM ), dot( d ) {}
          virtual int evaluate() { result_num = dot -> ticks(); return TOK_NUM; }
        };
        return new dot_ticks_remain_expr_t( a, dot );
      }
      else if ( splits[ 2 ] == "ticking" )
      {
        struct dot_ticking_expr_t : public action_expr_t
        {
          dot_t* dot;
          dot_ticking_expr_t( action_t* a, dot_t* d ) : action_expr_t( a, "dot_ticking", TOK_NUM ), dot( d ) {}
          virtual int evaluate() { result_num = dot -> ticking; return TOK_NUM; }
        };
        return new dot_ticking_expr_t( a, dot );
      }
    }
    else if ( splits[ 0 ] == "swing" )
    {
      std::string& s = splits[ 1 ];
      int hand = SLOT_NONE;
      if ( s == "mh" || s == "mainhand" || s == "main_hand" ) hand = SLOT_MAIN_HAND;
      if ( s == "oh" || s ==  "offhand" || s ==  "off_hand" ) hand = SLOT_OFF_HAND;
      if ( hand == SLOT_NONE ) return 0;
      if ( splits[ 2 ] == "remains" )
      {
        struct swing_remains_expr_t : public action_expr_t
        {
          int slot;
          swing_remains_expr_t( action_t* a, int s ) : action_expr_t( a, "swing_remains", TOK_NUM ), slot( s ) {}
          virtual int evaluate()
          {
            result_num = 9999;
            player_t* p = action -> player;
            attack_t* attack = ( slot == SLOT_MAIN_HAND ) ? p -> main_hand_attack : p -> off_hand_attack;
            if ( attack && attack -> execute_event ) result_num = attack -> execute_event -> remains();
            return TOK_NUM;
          }
        };
        return new swing_remains_expr_t( a, hand );
      }
    }
    else if ( splits[ 0 ] == "action" )
    {
      std::vector<action_t*> in_flight_list;
      for ( action_t* action = action_list; action; action = action -> next )
      {
        if ( action -> name_str == splits[ 1 ] )
        {
          if ( splits[ 2 ] == "in_flight" )
          {
            in_flight_list.push_back( action );
          }
          else
          {
            return action -> create_expression( splits[ 2 ] );
          }
        }
      }
      if ( in_flight_list.size() > 0 )
      {
        struct in_flight_multi_expr_t : public action_expr_t
        {
          std::vector<action_t*> action_list;
          in_flight_multi_expr_t( std::vector<action_t*> al ) : action_expr_t( al[ 0 ], "in_flight", TOK_NUM ), action_list( al ) {}
          virtual int evaluate()
          {
            result_num = false;
            for ( int i = 0; i < ( int ) action_list.size(); i++ )
            {
              if ( action_list[ i ] -> travel_event != NULL ) result_num = true;
            }
            return TOK_NUM;
          }
        };
        return new in_flight_multi_expr_t( in_flight_list );
      }
    }
  }
  else if ( num_splits == 2 )
  {
    if ( splits[ 0 ] == "set_bonus" )
    {
      return a -> player -> set_bonus.create_expression( a, splits[ 1 ] );
    }
  }



  return sim -> create_expression( a, name_str );
}

// player_t::create_profile =================================================

bool player_t::create_profile( std::string& profile_str, int save_type, bool save_html )
{
  std::string term;

  if ( save_html )
    term = "<br>\n";
  else
    term = "\n";

  if ( save_type == SAVE_ALL )
  {
    profile_str += "#!./simc " + term + term;
  }

  if ( ! comment_str.empty() )
  {
    profile_str += "# " + comment_str + term;
  }

  if ( save_type == SAVE_ALL )
  {
    std::string pname = name_str;

    profile_str += util_t::player_type_string( type );
    profile_str += "=" + util_t::format_text( pname, sim -> input_is_utf8 ) + term;
    profile_str += "origin=\"" + origin_str + "\"" + term;
    profile_str += "level=" + util_t::to_string( level ) + term;
    profile_str += "race=" + race_str + term;
    profile_str += "role=";
    profile_str += util_t::role_type_string( primary_role() ) + term;
    profile_str += "use_pre_potion=" + util_t::to_string( use_pre_potion ) + term;

    if ( professions_str.size() > 0 )
    {
      profile_str += "professions=" + professions_str + term;
    };
  }

  if ( save_type == SAVE_ALL || save_type == SAVE_TALENTS )
  {
    talents_str = "http://www.wowhead.com/talent#";
    talents_str += util_t::player_type_string( type );
    talents_str += "-";
    // This is necessary because sometimes the talent trees change shape between live/ptr.
    for( int i=0; i < MAX_TALENT_TREES; i++ )
    {
      for( unsigned j = 0; j < talent_trees[ i ].size(); j++ )
      {
        talent_t* t = talent_trees[ i ][ j ];
        std::stringstream ss;
        ss << t -> rank();
        talents_str += ss.str();
      }
    }

    if ( talents_str.size() > 0 )
    {
      profile_str += "talents=" + talents_str + term;
    };



    if ( glyphs_str.size() > 0 )
    {
      profile_str += "glyphs=" + glyphs_str + term;
    }
  }

  if ( save_type == SAVE_ALL || save_type == SAVE_ACTIONS )
  {
    if ( action_list_str.size() > 0 )
    {
      int i = 0;
      for ( action_t* a = action_list; a; a = a -> next )
      {
        if ( a -> signature_str.empty() ) continue;
        profile_str += "actions";
        profile_str += i ? "+=/" : "=";
        profile_str += a -> signature_str + term;
        i++;
      }
    }
  }

  if ( save_type == SAVE_ALL || save_type == SAVE_GEAR )
  {
    for ( int i=0; i < SLOT_MAX; i++ )
    {
      item_t& item = items[ i ];

      if ( item.active() )
      {
        profile_str += item.slot_name();
        profile_str += "=" + item.options_str + term;
      }
    }
    if ( ! items_str.empty() )
    {
      profile_str += "items=" + items_str + term;
    }

    profile_str += "# Gear Summary\n";
    for ( int i=0; i < STAT_MAX; i++ )
    {
      double value = initial_stats.get_stat( i );
      if ( value != 0 )
      {
        profile_str += "# gear_";
        profile_str += util_t::stat_type_string( i );
        profile_str += "=" + util_t::to_string( value, 0 ) + term;
      }
    }
    if ( meta_gem != META_GEM_NONE )
    {
      profile_str += "# meta_gem=";
      profile_str += util_t::meta_gem_type_string( meta_gem );
      profile_str += term;
    }

    if ( set_bonus.tier11_2pc_caster() ) profile_str += "# tier11_2pc_caster=1" + term;
    if ( set_bonus.tier11_4pc_caster() ) profile_str += "# tier11_4pc_caster=1" + term;
    if ( set_bonus.tier11_2pc_melee()  ) profile_str += "# tier11_2pc_melee=1" + term;
    if ( set_bonus.tier11_4pc_melee()  ) profile_str += "# tier11_4pc_melee=1" + term;
    if ( set_bonus.tier11_2pc_tank()   ) profile_str += "# tier11_2pc_tank=1" + term;
    if ( set_bonus.tier11_4pc_tank()   ) profile_str += "# tier11_4pc_tank=1" + term;
    if ( set_bonus.tier11_2pc_heal()   ) profile_str += "# tier11_2pc_heal=1" + term;
    if ( set_bonus.tier11_4pc_heal()   ) profile_str += "# tier11_4pc_heal=1" + term;

    if ( set_bonus.tier12_2pc_caster() ) profile_str += "# tier12_2pc_caster=1" + term;
    if ( set_bonus.tier12_4pc_caster() ) profile_str += "# tier12_4pc_caster=1" + term;
    if ( set_bonus.tier12_2pc_melee()  ) profile_str += "# tier12_2pc_melee=1" + term;
    if ( set_bonus.tier12_4pc_melee()  ) profile_str += "# tier12_4pc_melee=1" + term;
    if ( set_bonus.tier12_2pc_tank()   ) profile_str += "# tier12_2pc_tank=1" + term;
    if ( set_bonus.tier12_4pc_tank()   ) profile_str += "# tier12_4pc_tank=1" + term;
    if ( set_bonus.tier12_2pc_heal()   ) profile_str += "# tier12_2pc_heal=1" + term;
    if ( set_bonus.tier12_4pc_heal()   ) profile_str += "# tier12_4pc_heal=1" + term;

    if ( set_bonus.tier13_2pc_caster() ) profile_str += "# tier13_2pc_caster=1" + term;
    if ( set_bonus.tier13_4pc_caster() ) profile_str += "# tier13_4pc_caster=1" + term;
    if ( set_bonus.tier13_2pc_melee()  ) profile_str += "# tier13_2pc_melee=1" + term;
    if ( set_bonus.tier13_4pc_melee()  ) profile_str += "# tier13_4pc_melee=1" + term;
    if ( set_bonus.tier13_2pc_tank()   ) profile_str += "# tier13_2pc_tank=1" + term;
    if ( set_bonus.tier13_4pc_tank()   ) profile_str += "# tier13_4pc_tank=1" + term;
    if ( set_bonus.tier13_2pc_heal()   ) profile_str += "# tier13_2pc_heal=1" + term;
    if ( set_bonus.tier13_4pc_heal()   ) profile_str += "# tier13_4pc_heal=1" + term;

    if ( set_bonus.tier14_2pc_caster() ) profile_str += "# tier14_2pc_caster=1" + term;
    if ( set_bonus.tier14_4pc_caster() ) profile_str += "# tier14_4pc_caster=1" + term;
    if ( set_bonus.tier14_2pc_melee()  ) profile_str += "# tier14_2pc_melee=1" + term;
    if ( set_bonus.tier14_4pc_melee()  ) profile_str += "# tier14_4pc_melee=1" + term;
    if ( set_bonus.tier14_2pc_tank()   ) profile_str += "# tier14_2pc_tank=1" + term;
    if ( set_bonus.tier14_4pc_tank()   ) profile_str += "# tier14_4pc_tank=1" + term;
    if ( set_bonus.tier14_2pc_heal()   ) profile_str += "# tier14_2pc_heal=1" + term;
    if ( set_bonus.tier14_4pc_heal()   ) profile_str += "# tier14_4pc_heal=1" + term;

    for ( int i=0; i < SLOT_MAX; i++ )
    {
      item_t& item = items[ i ];
      if ( ! item.active() ) continue;
      if ( item.unique || item.unique_enchant || item.unique_addon || ! item.encoded_weapon_str.empty() )
      {
        profile_str += "# ";
        profile_str += item.slot_name();
        profile_str += "=";
        profile_str += item.name();
        if ( item.heroic() ) profile_str += ",heroic=1";
        if ( ! item.encoded_weapon_str.empty() ) profile_str += ",weapon=" + item.encoded_weapon_str;
        if ( item.unique_enchant ) profile_str += ",enchant=" + item.encoded_enchant_str;
        if ( item.unique_addon   ) profile_str += ",addon="   + item.encoded_addon_str;
        profile_str += term;
      }
    }

  }

  return true;
}

// player_t::copy_from =================================================

void player_t::copy_from( player_t* source )
{
  origin_str = source -> origin_str;
  level = source -> level;
  race_str = source -> race_str;
  role = source -> role;
  use_pre_potion = source -> use_pre_potion;
  professions_str = source -> professions_str;
  talents_str = "http://www.wowhead.com/talent#";
  talents_str += util_t::player_type_string( type );
  talents_str += "-";
  // This is necessary because sometimes the talent trees change shape between live/ptr.
  for( int i=0; i < MAX_TALENT_TREES; i++ )
  {
    for( unsigned j = 0; j < talent_trees[ i ].size(); j++ )
    {
      talent_t* t = talent_trees[ i ][ j ];
      talent_t* source_t = source -> find_talent( t -> td -> name_cstr() );
      if( source_t ) t -> set_rank( source_t -> rank() );
      std::stringstream ss;
      ss << t -> rank();
      talents_str += ss.str();
    }
  }
  glyphs_str = source -> glyphs_str;
  action_list_str = source -> action_list_str;
  int num_items = ( int ) items.size();
  for ( int i=0; i < num_items; i++ )
  {
    items[ i ] = source -> items[ i ];
    items[ i ].player = this;
  }
  gear = source -> gear;
  enchant = source -> enchant;
}

// player_t::create_options =================================================

void player_t::create_options()
{
  option_t player_options[] =
  {
    // General
    { "name",                                 OPT_STRING,   &( name_str                               ) },
    { "origin",                               OPT_STRING,   &( origin_str                             ) },
    { "region",                               OPT_STRING,   &( region_str                             ) },
    { "server",                               OPT_STRING,   &( server_str                             ) },
    { "id",                                   OPT_STRING,   &( id_str                                 ) },
    { "talents",                              OPT_FUNC,     ( void* ) ::parse_talent_url                },
    { "glyphs",                               OPT_STRING,   &( glyphs_str                             ) },
    { "race",                                 OPT_STRING,   &( race_str                               ) },
    { "level",                                OPT_INT,      &( level                                  ) },
    { "use_pre_potion",                       OPT_INT,      &( use_pre_potion                         ) },
    { "role",                                 OPT_FUNC,     ( void* ) ::parse_role_string               },
    { "target",                               OPT_STRING,   &( target_str                             ) },
    { "skill",                                OPT_FLT,      &( initial_skill                          ) },
    { "distance",                             OPT_FLT,      &( distance                               ) },
    { "professions",                          OPT_STRING,   &( professions_str                        ) },
    { "actions",                              OPT_STRING,   &( action_list_str                        ) },
    { "actions+",                             OPT_APPEND,   &( action_list_str                        ) },
    { "sleeping",                             OPT_BOOL,     &( sleeping                               ) },
    { "quiet",                                OPT_BOOL,     &( quiet                                  ) },
    { "save",                                 OPT_STRING,   &( save_str                               ) },
    { "save_gear",                            OPT_STRING,   &( save_gear_str                          ) },
    { "save_talents",                         OPT_STRING,   &( save_talents_str                       ) },
    { "save_actions",                         OPT_STRING,   &( save_actions_str                       ) },
    { "comment",                              OPT_STRING,   &( comment_str                            ) },
    { "bugs",                                 OPT_BOOL,     &( bugs                                   ) },
    { "world_lag",                            OPT_FUNC,     ( void* ) ::parse_world_lag                 },
    { "world_lag_stddev",                     OPT_FUNC,     ( void* ) ::parse_world_lag_stddev          },
    { "brain_lag",                            OPT_FUNC,     ( void* ) ::parse_brain_lag                 },
    { "brain_lag_stddev",                     OPT_FUNC,     ( void* ) ::parse_brain_lag_stddev          },
    // Items
    { "meta_gem",                             OPT_STRING,   &( meta_gem_str                           ) },
    { "items",                                OPT_STRING,   &( items_str                              ) },
    { "items+",                               OPT_APPEND,   &( items_str                              ) },
    { "head",                                 OPT_STRING,   &( items[ SLOT_HEAD      ].options_str    ) },
    { "neck",                                 OPT_STRING,   &( items[ SLOT_NECK      ].options_str    ) },
    { "shoulders",                            OPT_STRING,   &( items[ SLOT_SHOULDERS ].options_str    ) },
    { "shoulder",                             OPT_STRING,   &( items[ SLOT_SHOULDERS ].options_str    ) },
    { "shirt",                                OPT_STRING,   &( items[ SLOT_SHIRT     ].options_str    ) },
    { "chest",                                OPT_STRING,   &( items[ SLOT_CHEST     ].options_str    ) },
    { "waist",                                OPT_STRING,   &( items[ SLOT_WAIST     ].options_str    ) },
    { "legs",                                 OPT_STRING,   &( items[ SLOT_LEGS      ].options_str    ) },
    { "leg",                                  OPT_STRING,   &( items[ SLOT_LEGS      ].options_str    ) },
    { "feet",                                 OPT_STRING,   &( items[ SLOT_FEET      ].options_str    ) },
    { "foot",                                 OPT_STRING,   &( items[ SLOT_FEET      ].options_str    ) },
    { "wrists",                               OPT_STRING,   &( items[ SLOT_WRISTS    ].options_str    ) },
    { "wrist",                                OPT_STRING,   &( items[ SLOT_WRISTS    ].options_str    ) },
    { "hands",                                OPT_STRING,   &( items[ SLOT_HANDS     ].options_str    ) },
    { "hand",                                 OPT_STRING,   &( items[ SLOT_HANDS     ].options_str    ) },
    { "finger1",                              OPT_STRING,   &( items[ SLOT_FINGER_1  ].options_str    ) },
    { "finger2",                              OPT_STRING,   &( items[ SLOT_FINGER_2  ].options_str    ) },
    { "ring1",                                OPT_STRING,   &( items[ SLOT_FINGER_1  ].options_str    ) },
    { "ring2",                                OPT_STRING,   &( items[ SLOT_FINGER_2  ].options_str    ) },
    { "trinket1",                             OPT_STRING,   &( items[ SLOT_TRINKET_1 ].options_str    ) },
    { "trinket2",                             OPT_STRING,   &( items[ SLOT_TRINKET_2 ].options_str    ) },
    { "back",                                 OPT_STRING,   &( items[ SLOT_BACK      ].options_str    ) },
    { "main_hand",                            OPT_STRING,   &( items[ SLOT_MAIN_HAND ].options_str    ) },
    { "off_hand",                             OPT_STRING,   &( items[ SLOT_OFF_HAND  ].options_str    ) },
    { "ranged",                               OPT_STRING,   &( items[ SLOT_RANGED    ].options_str    ) },
    { "tabard",                               OPT_STRING,   &( items[ SLOT_TABARD    ].options_str    ) },
    // Set Bonus
    { "tier11_2pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T11_2PC_CASTER ]  ) },
    { "tier11_4pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T11_4PC_CASTER ]  ) },
    { "tier11_2pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T11_2PC_MELEE ]   ) },
    { "tier11_4pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T11_4PC_MELEE ]   ) },
    { "tier11_2pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T11_2PC_TANK ]    ) },
    { "tier11_4pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T11_4PC_TANK ]    ) },
    { "tier11_2pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T11_2PC_HEAL ]    ) },
    { "tier11_4pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T11_4PC_HEAL ]    ) },
    { "tier12_2pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T12_2PC_CASTER ]  ) },
    { "tier12_4pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T12_4PC_CASTER ]  ) },
    { "tier12_2pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T12_2PC_MELEE ]   ) },
    { "tier12_4pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T12_4PC_MELEE ]   ) },
    { "tier12_2pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T12_2PC_TANK ]    ) },
    { "tier12_4pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T12_4PC_TANK ]    ) },
    { "tier12_2pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T12_2PC_HEAL ]    ) },
    { "tier12_4pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T12_4PC_HEAL ]    ) },
    { "tier13_2pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T13_2PC_CASTER ]  ) },
    { "tier13_4pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T13_4PC_CASTER ]  ) },
    { "tier13_2pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T13_2PC_MELEE ]   ) },
    { "tier13_4pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T13_4PC_MELEE ]   ) },
    { "tier13_2pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T13_2PC_TANK ]    ) },
    { "tier13_4pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T13_4PC_TANK ]    ) },
    { "tier13_2pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T13_2PC_HEAL ]    ) },
    { "tier13_4pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T13_4PC_HEAL ]    ) },
    { "tier14_2pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T14_2PC_CASTER ]  ) },
    { "tier14_4pc_caster",                    OPT_BOOL,     &( set_bonus.count[ SET_T14_4PC_CASTER ]  ) },
    { "tier14_2pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T14_2PC_MELEE ]   ) },
    { "tier14_4pc_melee",                     OPT_BOOL,     &( set_bonus.count[ SET_T14_4PC_MELEE ]   ) },
    { "tier14_2pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T14_2PC_TANK ]    ) },
    { "tier14_4pc_tank",                      OPT_BOOL,     &( set_bonus.count[ SET_T14_4PC_TANK ]    ) },
    { "tier14_2pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T14_2PC_HEAL ]    ) },
    { "tier14_4pc_heal",                      OPT_BOOL,     &( set_bonus.count[ SET_T14_4PC_HEAL ]    ) },
    // Gear Stats
    { "gear_strength",                        OPT_FLT,  &( gear.attribute[ ATTR_STRENGTH  ]           ) },
    { "gear_agility",                         OPT_FLT,  &( gear.attribute[ ATTR_AGILITY   ]           ) },
    { "gear_stamina",                         OPT_FLT,  &( gear.attribute[ ATTR_STAMINA   ]           ) },
    { "gear_intellect",                       OPT_FLT,  &( gear.attribute[ ATTR_INTELLECT ]           ) },
    { "gear_spirit",                          OPT_FLT,  &( gear.attribute[ ATTR_SPIRIT    ]           ) },
    { "gear_spell_power",                     OPT_FLT,  &( gear.spell_power                           ) },
    { "gear_mp5",                             OPT_FLT,  &( gear.mp5                                   ) },
    { "gear_attack_power",                    OPT_FLT,  &( gear.attack_power                          ) },
    { "gear_expertise_rating",                OPT_FLT,  &( gear.expertise_rating                      ) },
    { "gear_haste_rating",                    OPT_FLT,  &( gear.haste_rating                          ) },
    { "gear_hit_rating",                      OPT_FLT,  &( gear.hit_rating                            ) },
    { "gear_crit_rating",                     OPT_FLT,  &( gear.crit_rating                           ) },
    { "gear_health",                          OPT_FLT,  &( gear.resource[ RESOURCE_HEALTH ]           ) },
    { "gear_mana",                            OPT_FLT,  &( gear.resource[ RESOURCE_MANA   ]           ) },
    { "gear_rage",                            OPT_FLT,  &( gear.resource[ RESOURCE_RAGE   ]           ) },
    { "gear_energy",                          OPT_FLT,  &( gear.resource[ RESOURCE_ENERGY ]           ) },
    { "gear_focus",                           OPT_FLT,  &( gear.resource[ RESOURCE_FOCUS  ]           ) },
    { "gear_runic",                           OPT_FLT,  &( gear.resource[ RESOURCE_RUNIC  ]           ) },
    { "gear_armor",                           OPT_FLT,  &( gear.armor                                 ) },
    { "gear_mastery_rating",                  OPT_FLT,  &( gear.mastery_rating                        ) },
    // Stat Enchants
    { "enchant_strength",                     OPT_FLT,  &( enchant.attribute[ ATTR_STRENGTH  ]        ) },
    { "enchant_agility",                      OPT_FLT,  &( enchant.attribute[ ATTR_AGILITY   ]        ) },
    { "enchant_stamina",                      OPT_FLT,  &( enchant.attribute[ ATTR_STAMINA   ]        ) },
    { "enchant_intellect",                    OPT_FLT,  &( enchant.attribute[ ATTR_INTELLECT ]        ) },
    { "enchant_spirit",                       OPT_FLT,  &( enchant.attribute[ ATTR_SPIRIT    ]        ) },
    { "enchant_spell_power",                  OPT_FLT,  &( enchant.spell_power                        ) },
    { "enchant_mp5",                          OPT_FLT,  &( enchant.mp5                                ) },
    { "enchant_attack_power",                 OPT_FLT,  &( enchant.attack_power                       ) },
    { "enchant_expertise_rating",             OPT_FLT,  &( enchant.expertise_rating                   ) },
    { "enchant_armor",                        OPT_FLT,  &( enchant.armor                              ) },
    { "enchant_haste_rating",                 OPT_FLT,  &( enchant.haste_rating                       ) },
    { "enchant_hit_rating",                   OPT_FLT,  &( enchant.hit_rating                         ) },
    { "enchant_crit_rating",                  OPT_FLT,  &( enchant.crit_rating                        ) },
    { "enchant_mastery_rating",               OPT_FLT,  &( enchant.mastery_rating                     ) },
    { "enchant_health",                       OPT_FLT,  &( enchant.resource[ RESOURCE_HEALTH ]        ) },
    { "enchant_mana",                         OPT_FLT,  &( enchant.resource[ RESOURCE_MANA   ]        ) },
    { "enchant_rage",                         OPT_FLT,  &( enchant.resource[ RESOURCE_RAGE   ]        ) },
    { "enchant_energy",                       OPT_FLT,  &( enchant.resource[ RESOURCE_ENERGY ]        ) },
    { "enchant_focus",                        OPT_FLT,  &( enchant.resource[ RESOURCE_FOCUS  ]        ) },
    { "enchant_runic",                        OPT_FLT,  &( enchant.resource[ RESOURCE_RUNIC  ]        ) },
    // Regen
    { "infinite_energy",                      OPT_BOOL,   &( infinite_resource[ RESOURCE_ENERGY ]     ) },
    { "infinite_focus",                       OPT_BOOL,   &( infinite_resource[ RESOURCE_FOCUS  ]     ) },
    { "infinite_health",                      OPT_BOOL,   &( infinite_resource[ RESOURCE_HEALTH ]     ) },
    { "infinite_mana",                        OPT_BOOL,   &( infinite_resource[ RESOURCE_MANA   ]     ) },
    { "infinite_rage",                        OPT_BOOL,   &( infinite_resource[ RESOURCE_RAGE   ]     ) },
    { "infinite_runic",                       OPT_BOOL,   &( infinite_resource[ RESOURCE_RUNIC  ]     ) },
    // Misc
    { "dtr_proc_chance",                      OPT_FLT,    &( dtr_proc_chance                          ) },
    { "dtr_base_proc_chance",                 OPT_FLT,    &( dtr_base_proc_chance                     ) },
    { "big_hitbox",                           OPT_BOOL,   &( big_hitbox                               ) },
    { "skip_actions",                         OPT_STRING, &( action_list_skip                         ) },
    { "modify_action",                        OPT_STRING, &( modify_action                            ) },
    { "elixirs",                              OPT_STRING, &( elixirs_str                              ) },
    { "flask",                                OPT_STRING, &( flask_str                                ) },
    { "food",                                 OPT_STRING, &( food_str                                 ) },
    { "player_resist_holy",                   OPT_INT,    &( spell_resistance[ SCHOOL_HOLY   ]        ) },
    { "player_resist_shadow",                 OPT_INT,    &( spell_resistance[ SCHOOL_SHADOW ]        ) },
    { "player_resist_arcane",                 OPT_INT,    &( spell_resistance[ SCHOOL_ARCANE ]        ) },
    { "player_resist_frost",                  OPT_INT,    &( spell_resistance[ SCHOOL_FROST  ]        ) },
    { "player_resist_fire",                   OPT_INT,    &( spell_resistance[ SCHOOL_FIRE   ]        ) },
    { "player_resist_nature",                 OPT_INT,    &( spell_resistance[ SCHOOL_NATURE ]        ) },
    { "reaction_time_mean",                   OPT_FLT,    &( reaction_mean                            ) },
    { "reaction_time_stddev",                 OPT_FLT,    &( reaction_stddev                          ) },
    { "reaction_time_nu",                     OPT_FLT,    &( reaction_nu                              ) },
    { NULL, OPT_UNKNOWN, NULL }
  };

  option_t::copy( options, player_options );
}

// player_t::create =========================================================

player_t* player_t::create( sim_t*             sim,
                            const std::string& type,
                            const std::string& name,
                            race_type r )
{
  if ( type == "death_knight" || type == "deathknight" )
  {
    return player_t::create_death_knight( sim, name, r );
  }
  else if ( type == "druid" )
  {
    return player_t::create_druid( sim, name, r );
  }
  else if ( type == "hunter" )
  {
    return player_t::create_hunter( sim, name, r );
  }
  else if ( type == "mage" )
  {
    return player_t::create_mage( sim, name, r );
  }
  else if ( type == "priest" )
  {
    return player_t::create_priest( sim, name, r );
  }
  else if ( type == "paladin" )
  {
    return player_t::create_paladin( sim, name, r );
  }
  else if ( type == "rogue" )
  {
    return player_t::create_rogue( sim, name, r );
  }
  else if ( type == "shaman" )
  {
    return player_t::create_shaman( sim, name, r );
  }
  else if ( type == "warlock" )
  {
    return player_t::create_warlock( sim, name, r );
  }
  else if ( type == "warrior" )
  {
    return player_t::create_warrior( sim, name, r );
  }
  else if ( type == "enemy" )
  {
    return player_t::create_enemy( sim, name, r );
  }
  return 0;
}

