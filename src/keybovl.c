#include <keybovl.h>
#if defined(SF2000)
#include <libretro.h>

extern retro_log_printf_t log_cb;
#endif

static keybovl_t*     ovl = NULL; // the current overlay
static keybovl_key_t* key;        // the selected key
static int            visible;    // true if the overlay is visible
static int            select;     // true if the select joy button is pressed
#if defined(SF2000)
static int keytab; // true if the tab key is pressed
#endif
static int            timer;      // milliseconds counter to release the key
static uint32_t       joystate = 0; // joystick button states
#if defined(SF2000)
static uint32_t keynavstate = 0; // keyboard navigation button states
#endif
static uint32_t       keystate[ ( RETROK_LAST + 31 ) / 32 ] = { 0 }; // key states

#define TOGGLE   1
#define PRESSED  2
#define SELECTED 4

static void draw_ts( uint16_t* fb, int pitch )
{
  const uint16_t* src = ovl->image;
  int x, y;
  
  for ( y = 0; y < 240; y++ )
  {
    uint16_t* save = fb;
    
    for ( x = 0; x < 320; x++ )
    {
      *fb++ = *src++;
    }

    fb = save + pitch;
  }
}

static void draw_tS( uint16_t* fb, int pitch )
{
  const uint16_t* src = ovl->image;
  int x, y;
  
  for ( y = 0; y < 240; y++ )
  {
    uint16_t* save = fb;
    
    for ( x = 0; x < 320; x++ )
    {
      uint32_t pixel = *src++;

      fb[         0 ] = pixel;
      fb[         1 ] = pixel;
      fb[ pitch + 0 ] = pixel;
      fb[ pitch + 1 ] = pixel;

      fb += 2;
    }

    fb = save + pitch * 2;
  }
}

static void draw_Ts( uint16_t* fb, int pitch )
{
  const uint16_t* src = ovl->image;
  int x, y;
  
  for ( y = 0; y < 240; y++ )
  {
    uint16_t* save = fb;
    
    for ( x = 0; x < 320; x++ )
    {
      uint32_t pixel = ( *src++ & 0xe79c ) * 3;
      *fb = ( pixel + ( *fb & 0xe79c ) ) >> 2;
      fb++;
    }

    fb = save + pitch;
  }
}

static void draw_TS( uint16_t* fb, int pitch )
{
  const uint16_t* src = ovl->image;
  int x, y;
  
  for ( y = 0; y < 240; y++ )
  {
    uint16_t* save = fb;
    
    for ( x = 0; x < 320; x++ )
    {
      uint32_t pixel = ( *src++ & 0xe79c ) * 3;

      fb[         0 ] = ( pixel + ( fb[         0 ] & 0xe79c ) ) >> 2;
      fb[         1 ] = ( pixel + ( fb[         1 ] & 0xe79c ) ) >> 2;
      fb[ pitch + 0 ] = ( pixel + ( fb[ pitch + 0 ] & 0xe79c ) ) >> 2;
      fb[ pitch + 1 ] = ( pixel + ( fb[ pitch + 1 ] & 0xe79c ) ) >> 2;

      fb += 2;
    }

    fb = save + pitch * 2;
  }
}

static void invert_s( uint16_t* fb, int pitch, int w, int h )
{
  int x, y;
  
  for ( y = 0; y < h; y++ )
  {
    uint16_t* save = fb;
    
    for ( x = 0; x < w; x++ )
    {
      *fb = ~*fb;
      fb++;
    }
    
    fb = save + pitch;
  }
}

static void invert_S( uint16_t* fb, int pitch, int w, int h )
{
  int x, y;
  
  for ( y = 0; y < h; y++ )
  {
    uint16_t* save = fb;
    
    for ( x = 0; x < w; x++ )
    {
      fb[         0 ] = ~fb[         0 ];
      fb[         1 ] = ~fb[         1 ];
      fb[ pitch + 0 ] = ~fb[ pitch + 0 ];
      fb[ pitch + 1 ] = ~fb[ pitch + 1 ];
      
      fb += 2;
    }
    
    fb = save + pitch * 2;
  }
}

static void draw( uint16_t* fb, int pitch, int transp, int scale )
{
  keybovl_key_t* k;
  
  if ( visible )
  {
    switch ( ( transp << 1 | scale ) & 3 )
    {
    case 0: draw_ts( fb, pitch ); break;
    case 1: draw_tS( fb, pitch ); break;
    case 2: draw_Ts( fb, pitch ); break;
    case 3: draw_TS( fb, pitch ); break;
    }
    
    key->meta |= SELECTED;
    
    for ( k = ovl->keys; k->id != 0xffff; k++ )
    {
      if ( k->meta & ( PRESSED | SELECTED ) )
      {
        uint16_t* dest;
        int x, y, w, h;
        
        ovl->getrect( ovl, k, &x, &y, &w, &h );
        
        if ( scale )
        {
          dest = fb + y * pitch * 2 + x * 2;
          invert_S( dest, pitch, w, h );
        }
        else
        {
          dest = fb + y * pitch + x;
          invert_s( dest, pitch, w, h );
        }
      }
    }
    
    key->meta &= ~SELECTED;
  }
}

static void depress( int dt )
{
  keybovl_key_t* k;
  
  if ( timer > 0 )
  {
    timer -= dt;
    
    if ( timer <= 0 )
    {
      ovl->keyrelease( ovl, key->mapped );
      
      for ( k = ovl->keys; k->id != 0xffff; k++ )
      {
        if ( k->meta & PRESSED )
        {
          ovl->keyrelease( ovl, k->mapped );
        }
      }
    }
  }
}

static keybovl_key_t* findkey( uint16_t id )
{
  keybovl_key_t* k;
  
  for ( k = ovl->keys; k->id != 0xffff; k++ )
  {
    if ( k->id == id )
    {
      return k;
    }
  }
  
  return ovl->keys;
}

#if !defined(SF2000)
static void update( retro_input_state_t input_cb, unsigned* devices, int ms )
{
  keybovl_key_t* k;
  unsigned       p, i;
  
  // Show/hide the virtual keyboard
  
  for ( p = 0; p < 2; p++ )
  {
    if ( ( devices[ p ] & RETRO_DEVICE_MASK ) != RETRO_DEVICE_JOYPAD )
    {
      continue;
    }
    
    if ( input_cb( p, devices[ p ], 0, RETRO_DEVICE_ID_JOYPAD_SELECT ) )
    {
      if ( !select )
      {
        select = 1;
        visible = !visible;
      }
    }
    else
    {
      select = 0;
    }
  }
  
  // Process the joypad
  
  for ( p = 0; p < 2; p++ )
  {
    if ( ( devices[ p ] & RETRO_DEVICE_MASK ) != RETRO_DEVICE_JOYPAD )
    {
      continue;
    }
    
    for ( i = 0; i < 16; i++ ) // where's RETRO_DEVICE_ID_JOYPAD_LAST? :P
    {
      if ( i == RETRO_DEVICE_ID_JOYPAD_SELECT )
      {
        continue;
      }
      
      int16_t  is_down = input_cb( p, devices[ p ], 0, i );
      uint32_t bit = 1 << i;
      
      if ( !visible )
      {
        // regular button press
        
        if ( is_down )
        {
          if ( ( joystate & bit ) == 0 )
          {
            joystate |= bit;
            
            if ( ovl->joymap[ i ] )
            {
              ovl->keypress( ovl, ovl->joymap[ i ] );
            }
          }
        }
        else
        {
          if ( joystate & bit )
          {
            joystate &= ~bit;
            
            if ( ovl->joymap[ i ] )
            {
              ovl->keyrelease( ovl, ovl->joymap[ i ] );
            }
          }
        }
      }
      else if ( timer <= 0 )
      {
        // virtual keyboard navigation
        
        if ( is_down )
        {
          if ( ( joystate & bit ) == 0 )
          {
            joystate |= bit;
            
            switch ( i )
            {
            case RETRO_DEVICE_ID_JOYPAD_UP:    key = findkey( key->up );    break;
            case RETRO_DEVICE_ID_JOYPAD_DOWN:  key = findkey( key->down );  break;
            case RETRO_DEVICE_ID_JOYPAD_LEFT:  key = findkey( key->left );  break;
            case RETRO_DEVICE_ID_JOYPAD_RIGHT: key = findkey( key->right ); break;
            
            case RETRO_DEVICE_ID_JOYPAD_A:
            case RETRO_DEVICE_ID_JOYPAD_B:
              {
                if ( key->meta & TOGGLE )
                {
                  key->meta ^= PRESSED;
                }
                else
                {
                  for ( k = ovl->keys; k->id != 0xffff; k++ )
                  {
                    if ( ( k->meta & ( PRESSED | TOGGLE ) ) == ( PRESSED | TOGGLE ) )
                    {
                      ovl->keypress( ovl, k->mapped );
                    }
                  }
                  
                  ovl->keypress( ovl, key->mapped );
                  timer = ms;
                }
              }
            }
          }
        }
        else
        {
          joystate &= ~bit;
        }
      }
    }
  }
  
  // Process the keyboard
  
  if ( !visible )
  {
    for ( p = 0; p < 2; p++ )
    {
      if ( ( devices[ p ] & RETRO_DEVICE_MASK ) != RETRO_DEVICE_KEYBOARD )
      {
        continue;
      }
      
      for ( k = ovl->keys; k->id != 0xffff; k++ )
      {
        int16_t  is_down = input_cb( p, devices[ p ], 0, k->retro );
        int      index = k->retro / 32;
        uint32_t bit = 1 << ( k->retro & 31 );

        if ( is_down )
        {
          if ( ( keystate[ index ] & bit ) == 0 )
          {
            keystate[ index ] |= bit;
            ovl->keypress( ovl, k->mapped );
          }
        }
        else
        {
          if ( keystate[ index ] & bit )
          {
            keystate[ index ] &= ~bit;
            ovl->keyrelease( ovl, k->mapped );
          }
        }
      }
    }
  }
}
#else
static void update_keyb( retro_input_state_t input_cb, int ms )
{
  keybovl_key_t* k;
  unsigned bit;
  int16_t is_down;
  int index;

  /* Toggle the virtual keyboard */
  if ( input_cb( 0, RETRO_DEVICE_KEYBOARD, 0, RETROK_TAB ) )
  {
    if ( !keytab )
    {
      visible = !visible;
      keytab = 1;
    }
  }
  else
  {
    keytab = 0;
  }

  /* Process the keyboard */

  if ( !visible )
  {
    /* Direct keyboard input */

    for ( k = ovl->keys; k->id != 0xffff; k++ )
    {

      is_down = input_cb( 0, RETRO_DEVICE_KEYBOARD, 0, k->retro );
      index = k->retro / 32;
      bit = 1 << ( k->retro & 31 );

      if ( is_down )
      {
        if ( ( keystate[ index ] & bit ) == 0 )
        {
          keystate[ index ] |= bit;
          ovl->keypress( ovl, k->mapped );
        }
      }
      else
      {
        if ( keystate[ index ] & bit )
        {
          keystate[ index ] &= ~bit;
          ovl->keyrelease( ovl, k->mapped );
        }
      }

    } /* for each key */
  }
  else if ( timer <= 0 )
  {
    /* virtual keyboard navigation with keyboard */

    int16_t is_down;

    /* left */
    bit = 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
    is_down = input_cb( 0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LEFT );
    if ( is_down )
    {
      if ( ( keynavstate & bit ) == 0 && ( joystate & bit ) == 0 )
      {
        keynavstate |= bit;
        key = findkey( key->left );
      }
    }
    else
      keynavstate &= ~bit;

    /* right */
    bit = 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    is_down = input_cb( 0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RIGHT );
    if ( is_down )
    {
      if ( ( keynavstate & bit ) == 0 && ( joystate & bit ) == 0 )
      {
        keynavstate |= bit;
        key = findkey( key->right );
      }
    }
    else
      keynavstate &= ~bit;
    /* up */
    bit = 1 << RETRO_DEVICE_ID_JOYPAD_UP;
    is_down = input_cb( 0, RETRO_DEVICE_KEYBOARD, 0, RETROK_UP );
    if ( is_down )
    {
      if ( ( keynavstate & bit ) == 0 && ( joystate & bit ) == 0 )
      {
        keynavstate |= bit;
        key = findkey( key->up );
      }
    }
    else
      keynavstate &= ~bit;

    /* down */
    bit = 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
    is_down = input_cb( 0, RETRO_DEVICE_KEYBOARD, 0, RETROK_DOWN );
    if ( is_down )
    {
      if ( ( keynavstate & bit ) == 0 && ( joystate & bit ) == 0 )
      {
        keynavstate |= bit;
        key = findkey( key->down );
      }
    }
    else
      keynavstate &= ~bit;

    /* enter */
    bit = 1 << RETRO_DEVICE_ID_JOYPAD_A;
    bit |= 1 << RETRO_DEVICE_ID_JOYPAD_B;
    is_down = input_cb( 0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RETURN );
    if ( is_down )
    {
      if ( ( keynavstate & bit ) == 0 && ( joystate & bit ) == 0 )
      {
        keynavstate |= bit;
        if ( key->meta & TOGGLE )
        {
          key->meta ^= PRESSED;
        }
        else
        {
          for ( k = ovl->keys; k->id != 0xffff; k++ )
          {
            if ( ( k->meta & ( PRESSED | TOGGLE ) ) == ( PRESSED | TOGGLE ) )
            {
              ovl->keypress( ovl, k->mapped );
            }
          }

          ovl->keypress( ovl, key->mapped );
          timer = ms;
        }
      }
    }
    else
    {
      keynavstate &= ~bit;
    }
  }
}

static void update_joy( retro_input_state_t input_cb, int ms )
{
  keybovl_key_t* k;
  unsigned ibtn, bit;
  int16_t is_btn_down;

  /* Toggle the virtual keyboard */
  if ( input_cb( 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT ) )
  {
    if ( !select )
    {
      visible = !visible;
      select = 1;
    }
  }
  else
  {
    select = 0;
  }

  /* Process the joypad */
  for ( ibtn = 0; ibtn < 16; ++ibtn ) /* where's RETRO_DEVICE_ID_JOYPAD_LAST? :P */
  {
    if ( ibtn == RETRO_DEVICE_ID_JOYPAD_SELECT )
      continue;

    is_btn_down = input_cb( 0, RETRO_DEVICE_JOYPAD, 0, ibtn ) ? 1 : 0;
    bit = 1 << ibtn;

    if ( !visible )
    {
      /* joystick -> keyboard mapper */

      if ( is_btn_down )
      {
        if ( ( joystate & bit ) == 0 )
        {
          joystate |= bit;

          if ( ovl->joymap[ ibtn ] )
          {
            ovl->keypress( ovl, ovl->joymap[ ibtn ] );
          }
        }
      }
      else
      {
        if ( joystate & bit )
        {
          joystate &= ~bit;

          if ( ovl->joymap[ ibtn ] )
          {
            ovl->keyrelease( ovl, ovl->joymap[ ibtn ] );
          }
        }
      }
    }
    else if ( timer <= 0 )
    {
      /* virtual keyboard navigation */

      if ( is_btn_down )
      {
        if ( ( joystate & bit ) == 0 )
        {
          joystate |= bit;

          switch ( ibtn )
          {

          case RETRO_DEVICE_ID_JOYPAD_UP:    key = findkey( key->up );    break;
          case RETRO_DEVICE_ID_JOYPAD_DOWN:  key = findkey( key->down );  break;
          case RETRO_DEVICE_ID_JOYPAD_LEFT:  key = findkey( key->left );  break;
          case RETRO_DEVICE_ID_JOYPAD_RIGHT: key = findkey( key->right ); break;

          case RETRO_DEVICE_ID_JOYPAD_A:
          case RETRO_DEVICE_ID_JOYPAD_B:
            {
              if ( key->meta & TOGGLE )
              {
                key->meta ^= PRESSED;
              }
              else
              {
                for ( k = ovl->keys; k->id != 0xffff; k++ )
                {
                  if ( ( k->meta & ( PRESSED | TOGGLE ) ) == ( PRESSED | TOGGLE ) )
                  {
                    ovl->keypress( ovl, k->mapped );
                  }
                }

                ovl->keypress( ovl, key->mapped );
                timer = ms;
              }
            }
            break;

          } /* switch ( ibtn ) */

        } /* if ( ( joystate & bit ) == 0 ) */
      }
      else
      {
        joystate &= ~bit;

      } /* if ( is_btn_down ) */
    }

  } /* for each joypad button */
}
#endif

void keybovl_set( keybovl_t* ovl_ )
{
  ovl = ovl_;
  key = ovl->keys;
  visible = 0;
  timer = 0;
}

#if !defined(SF2000)
void keybovl_update( retro_input_state_t input_cb, unsigned* devices, uint16_t* fb, int pitch, int transp, int scale, int ms, int dt )
{
  if ( ovl )
  {
    depress( dt );
    update( input_cb, devices, ms );
    draw( fb, pitch, transp, scale );
  }
}
#else
void keybovl_update( retro_input_state_t input_cb, unsigned input_device, uint16_t* fb, int pitch, int transp, int scale, int ms, int dt )
{
  if ( !ovl )
    return;

  depress( dt );

  if ( input_device == RETRO_DEVICE_KEYBOARD )
  {
    update_keyb( input_cb, ms );
  }
  else if ( input_device == RETRO_DEVICE_JOYPAD )
  {
    update_joy( input_cb, ms );
  }

  draw( fb, pitch, transp, scale );
}
#endif
