/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2009  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <assert.h>
#include <allegro.h>
/* #include <allegro/internal/aintern.h> */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "jinete/jinete.h"
#include "jinete/jintern.h"

#include "ase_exception.h"
#include "ui_context.h"
#include "commands/commands.h"
#include "commands/params.h"
#include "console.h"
#include "core/app.h"
#include "core/cfg.h"
#include "core/core.h"
#include "core/drop_files.h"
#include "core/file_system.h"
#include "core/modules.h"
#include "dialogs/options.h"
#include "dialogs/tips.h"
#include "file/file.h"
#include "intl/intl.h"
#include "modules/editors.h"
#include "modules/gfx.h"
#include "modules/gui.h"
#include "modules/palettes.h"
#include "modules/recent.h"
#include "modules/rootmenu.h"
#include "raster/image.h"
#include "raster/layer.h"
#include "raster/palette.h"
#include "raster/sprite.h"
#include "util/boundary.h"
#include "util/recscr.h"
#include "widgets/colbar.h"
#include "widgets/editor.h"
#include "widgets/menuitem.h"
#include "widgets/statebar.h"
#include "widgets/tabs.h"
#include "widgets/toolbar.h"

#ifdef ALLEGRO_WINDOWS
  #include <winalleg.h>
#endif

class Option
{
  int m_type;
  jstring m_data;

public:

  enum {
    OpenSprite,
  };

  Option(int type, const char* data)
  {
    m_type = type;
    m_data = data;
  }

  int type() const { return m_type; }
  const char* data() const { return m_data.c_str(); }

};

class App::Pimpl
{
public:
  const char* m_exe_name;
  std::vector<Option*> m_options;
  CommandsModule m_commands_modules;
  UIContext m_ui_context;
  int m_return_code;
  std::vector<std::vector<IAppHook*> > m_apphooks;

  Pimpl() { }
  ~Pimpl() { }
};

App* App::m_instance = NULL;

static JWidget top_window = NULL;     /* top level window (the desktop) */
static JWidget box_menubar = NULL;    /* box where the menu bar is */
static JWidget box_colorbar = NULL;   /* box where the color bar is */
static JWidget box_toolbar = NULL;    /* box where the tools bar is */
static JWidget box_statusbar = NULL;  /* box where the status bar is */
static JWidget box_tabsbar = NULL;    /* box where the tabs bar is */
static JWidget menubar = NULL;	      /* the menu bar widget */
static JWidget statusbar = NULL;      /* the status bar widget */
static JWidget colorbar = NULL;	      /* the color bar widget */
static JWidget toolbar = NULL;	      /* the tool bar widget */
static JWidget tabsbar = NULL;	      /* the tabs bar widget */

static char *palette_filename = NULL;

static void tabsbar_select_callback(JWidget tabs, void *data, int button);

/**
 * Initializes the application loading the modules, setting the
 * graphics mode, loading the configuration and resources, etc.
 */
App::App(int argc, char *argv[])
{
  assert(m_instance == NULL);
  m_instance = this;

  // create private implementation data
  m_pimpl = new Pimpl;
  m_pimpl->m_exe_name = argv[0];
  m_pimpl->m_return_code = 0;
  m_pimpl->m_apphooks.resize(AppEvent::NumEvents);

  /* initialize language suppport */
  intl_init();

  /* install the `core' of ASE application */
  core_init();

  /* install the file-system access module */
  file_system_init();

  /* init configuration */
  ase_config_init();

  // load the language file
  intl_load_lang();

  /* search options in the arguments */
  check_args(argc, argv);

  /* GUI is the default mode */
  if (!(ase_mode & MODE_BATCH))
    ase_mode |= MODE_GUI;

  /* install 'raster' stuff */
  gfxobj_init();

  // install the modules
  modules_init(ase_mode & MODE_GUI ? REQUIRE_INTERFACE: 0);

  /* custom default palette? */
  if (palette_filename) {
    Palette *pal;

    PRINTF("Loading custom palette file: %s\n", palette_filename);

    pal = palette_load(palette_filename);
    if (pal == NULL)
      throw ase_exception(std::string("Error loading default palette from: ")
			  + palette_filename);

    set_default_palette(pal);
    palette_free(pal);
  }

  /* set system palette to the default one */
  set_current_palette(NULL, TRUE);
}

/**
 * Runs the ASE application. In GUI mode it's the top-level window, in
 * console/scripting it just runs the specified scripts.
 */
int App::run()
{
  /* initialize GUI interface */
  if (ase_mode & MODE_GUI) {
    JWidget view, editor;

    PRINTF("GUI mode\n");

    /* setup the GUI screen */
    jmouse_set_cursor(JI_CURSOR_NORMAL);
    jmanager_refresh_screen();

    /* load main window */
    top_window = load_widget("main.jid", "main_window");
    if (!top_window) {
      allegro_message("Error loading data data/jids/main.jid file.\n"
		      "You have to reinstall the program.\n");
      return 1;
    }

    box_menubar = jwidget_find_name(top_window, "menubar");
    box_editors = jwidget_find_name(top_window, "editor");
    box_colorbar = jwidget_find_name(top_window, "colorbar");
    box_toolbar = jwidget_find_name(top_window, "toolbar");
    box_statusbar = jwidget_find_name(top_window, "statusbar");
    box_tabsbar = jwidget_find_name(top_window, "tabsbar");

    menubar = jmenubar_new();
    statusbar = statusbar_new();
    colorbar = colorbar_new(box_colorbar->align());
    toolbar = toolbar_new();
    tabsbar = tabs_new(tabsbar_select_callback);
    view = editor_view_new();
    editor = create_new_editor();

    // configure all widgets to expansives
    jwidget_expansive(menubar, TRUE);
    jwidget_expansive(statusbar, TRUE);
    jwidget_expansive(colorbar, TRUE);
    jwidget_expansive(toolbar, TRUE);
    jwidget_expansive(tabsbar, TRUE);
    jwidget_expansive(view, TRUE);

    /* prepare the first editor */
    jview_attach(view, editor);

    /* setup the menus */
    jmenubar_set_menu(menubar, get_root_menu());

    /* start text of status bar */
    app_default_statusbar_message();

    /* add the widgets in the boxes */
    if (box_menubar) jwidget_add_child(box_menubar, menubar);
    if (box_editors) jwidget_add_child(box_editors, view);
    if (box_colorbar) jwidget_add_child(box_colorbar, colorbar);
    if (box_toolbar) jwidget_add_child(box_toolbar, toolbar);
    if (box_statusbar) jwidget_add_child(box_statusbar, statusbar);
    if (box_tabsbar) jwidget_add_child(box_tabsbar, tabsbar);

    /* prepare the window */
    jwindow_remap(top_window);

    /* rebuild menus */
    app_realloc_sprite_list();
    app_realloc_recent_list();

    /* set current editor */
    set_current_editor(editor);

    /* open the window */
    jwindow_open(top_window);

    /* refresh the screen */
    jmanager_refresh_screen();
  }

  /* set background mode for non-GUI modes */
/*   if (!(ase_mode & MODE_GUI)) */
/*     set_display_switch_mode(SWITCH_BACKAMNESIA); */
    set_display_switch_mode(SWITCH_BACKGROUND);

    // procress options
  PRINTF("Processing options...\n");
  
  for (std::vector<Option*>::iterator
	 it = m_pimpl->m_options.begin(); it != m_pimpl->m_options.end(); ++it) {
    Option* option = *it;

    switch (option->type()) {

      case Option::OpenSprite: {
	/* load the sprite */
	Sprite *sprite = sprite_load(option->data());
	if (!sprite) {
	  /* error */
	  if (ase_mode & MODE_GUI)
	    jalert(_("Error<<Error loading file \"%s\"||&Close"), option->data());
	  else
	    user_printf(_("Error loading file \"%s\"\n"), option->data());
	}
	else {
	  /* mount and select the sprite */
	  UIContext* context = UIContext::instance();
	  context->add_sprite(sprite);
	  context->set_current_sprite(sprite);

	  if (ase_mode & MODE_GUI) {
	    /* show it */
	    set_sprite_in_more_reliable_editor(context->get_first_sprite());

	    /* recent file */
	    recent_file(option->data());
	  }
	}
	break;
      }
    }
    delete option;
  }
  m_pimpl->m_options.clear();

  /* just batch mode */
  if (ase_mode & MODE_BATCH) {
    PRINTF("Batch mode\n");
  }
  /* run the GUI */
  else if (ase_mode & MODE_GUI) {
    /* select language */
    dialogs_select_language(FALSE);

    // show tips only if there are not a current sprite
    if (!UIContext::instance()->get_current_sprite())
      dialogs_tips(FALSE);

    // support to drop files from Windows explorer
    install_drop_files();

    gui_run();

    uninstall_drop_files();

    /* stop recording */
    if (is_rec_screen())
      rec_screen_off();

    /* remove the root-menu from the menu-bar (because the rootmenu
       module should destroy it) */
    jmenubar_set_menu(menubar, NULL);

    /* destroy the top-window */
    jwidget_free(top_window);
    top_window = NULL;
  }
  return 0;
}

/**
 * Finishes the ASE application.
 */
App::~App()
{
  try {
    assert(m_instance == this);

    // remove ase handlers
    PRINTF("Uninstalling ASE\n");

    App::trigger_event(AppEvent::Exit);

    // destroy application hooks
    for (int c=0; c<AppEvent::NumEvents; ++c) {
      for (std::vector<IAppHook*>::iterator
	     it = m_pimpl->m_apphooks[c].begin(); 
	   it != m_pimpl->m_apphooks[c].end(); ++it) {
	IAppHook* apphook = *it;
	delete apphook;
      }

      // clear the list of hooks (so nobody can call the deleted hooks)
      m_pimpl->m_apphooks[c].clear();
    }

    // finalize modules, configuration and core
    modules_exit();
    delete m_pimpl;
    editor_cursor_exit();
    boundary_exit();

    gfxobj_exit();
    ase_config_exit();
    file_system_exit();
    core_exit();
    intl_exit();
  
    m_instance = NULL;
  }
  catch (...) {
    allegro_message("Uncaught exception in ~App");
    // no throw
  }
}

void App::add_hook(AppEvent::Type event, IAppHook* hook)
{
  assert(event >= 0 && event < AppEvent::NumEvents);

  m_pimpl->m_apphooks[event].push_back(hook);
}

void App::trigger_event(AppEvent::Type event)
{
  assert(event >= 0 && event < AppEvent::NumEvents);

  for (std::vector<IAppHook*>::iterator
	 it = m_pimpl->m_apphooks[event].begin();
       it != m_pimpl->m_apphooks[event].end(); ++it) {
    IAppHook* apphook = *it;
    apphook->on_event();
  }
}

/**
 * Updates palette and redraw the screen.
 */
void app_refresh_screen(const Sprite* sprite)
{
  assert(screen != NULL);

  if (sprite)
    set_current_palette(sprite_get_palette(sprite, sprite->frame), false);
  else
    set_current_palette(NULL, false);

  // redraw the screen
  jmanager_refresh_screen();
}

/**
 * Regenerates the label for each tab in the @em tabsbar.
 */
void app_realloc_sprite_list()
{
  UIContext* context = UIContext::instance();
  const SpriteList& list = context->get_sprite_list();

  /* insert all other sprites */
  for (SpriteList::const_iterator
	 it = list.begin(); it != list.end(); ++it) {
    Sprite* sprite = *it;
    tabs_set_text_for_tab(tabsbar, get_filename(sprite->filename), sprite);
  }
}

/**
 * Updates the recent list menu.
 *
 * @warning This routine can't be used when a menu callback was
 * called, because, it destroy the menus, you should use
 * schedule_rebuild_recent_list() instead (src/modules/gui.c).
 */
bool app_realloc_recent_list()
{
  JWidget list_menuitem = get_recent_list_menuitem();
  JWidget menuitem;

  /* update the recent file list menu item */
  if (list_menuitem) {
    if (jmenuitem_has_submenu_opened(list_menuitem))
      return FALSE;

    Command *cmd_open_file = CommandsModule::instance()->get_command_by_name(CommandId::open_file);

    JWidget submenu = jmenuitem_get_submenu(list_menuitem);
    if (submenu) {
      jmenuitem_set_submenu(list_menuitem, NULL);
      jwidget_free(submenu);
    }

    submenu = jmenu_new();
    jmenuitem_set_submenu(list_menuitem, submenu);

    if (jlist_first(get_recent_files_list())) {
      const char *filename;
      Params params;
      JLink link;

      JI_LIST_FOR_EACH(get_recent_files_list(), link) {
	filename = reinterpret_cast<const char*>(link->data);

	params.set("filename", filename);

	menuitem = menuitem_new(get_filename(filename), cmd_open_file, &params);
	jwidget_add_child(submenu, menuitem);
      }
    }
    else {
      menuitem = menuitem_new(_("Nothing"), NULL, NULL);
      jwidget_disable(menuitem);
      jwidget_add_child(submenu, menuitem);
    }
  }

  return TRUE;
}

int app_get_current_image_type()
{
  Context* context = UIContext::instance();
  assert(context != NULL);

  Sprite* sprite = context->get_current_sprite();
  if (sprite != NULL)
    return sprite->imgtype;
  else if (screen != NULL && bitmap_color_depth(screen) == 8)
    return IMAGE_INDEXED;
  else
    return IMAGE_RGB;
}

JWidget app_get_top_window() { return top_window; }
JWidget app_get_menubar() { return menubar; }
JWidget app_get_statusbar() { return statusbar; }
JWidget app_get_colorbar() { return colorbar; }
JWidget app_get_toolbar() { return toolbar; }
JWidget app_get_tabsbar() { return tabsbar; }

void app_default_statusbar_message()
{
  statusbar_set_text(app_get_statusbar(), 250,
		     "ASE " VERSION ", " COPYRIGHT);
}

int app_get_fg_color(Sprite *sprite)
{
  color_t c = colorbar_get_fg_color(colorbar);
  assert(sprite != NULL);

  if (sprite->layer != NULL)
    return get_color_for_layer(sprite->layer, c);
  else
    return get_color_for_image(sprite->imgtype, c);
}

int app_get_bg_color(Sprite *sprite)
{
  color_t c = colorbar_get_bg_color(colorbar);
  assert(sprite != NULL);

  if (sprite->layer != NULL)
    return get_color_for_layer(sprite->layer, c);
  else
    return get_color_for_image(sprite->imgtype, c);
}

int app_get_color_to_clear_layer(Layer *layer)
{
  /* all transparent layers are cleared with the mask color */
  color_t color = color_mask();
  
  /* the `Background' is erased with the `Background Color' */
  if (layer != NULL && layer->is_background())
    color = colorbar_get_bg_color(colorbar);

  return get_color_for_layer(layer, color);
}

static void tabsbar_select_callback(JWidget tabs, void *data, int button)
{
  // Note: data can be NULL (the "Nothing" tab)
  Sprite* sprite = (Sprite*)data;

  // put as current sprite
  set_sprite_in_more_reliable_editor(sprite);

  // middle-button: close the sprite
  if (data && (button & 4)) {
    Command* close_file_cmd =
      CommandsModule::instance()->get_command_by_name(CommandId::close_file);

    UIContext::instance()->execute_command(close_file_cmd, NULL);
  }
}

/**
 * Looks the inpunt arguments in the command line.
 */
void App::check_args(int argc, char *argv[])
{
  Console console;
  int i, n, len;
  char *arg;

  for (i=1; i<argc; i++) {
    arg = argv[i];

    for (n=0; arg[n] == '-'; n++);
    len = strlen(arg+n);

    /* option */
    if ((n > 0) && (len > 0)) {
      /* use other palette file */
      if (strncmp(arg+n, "palette", len) == 0) {
        if (++i < argc)
	  palette_filename = argv[i];
        else
          usage(false);
      }
      /* video resolution */
      else if (strncmp(arg+n, "resolution", len) == 0) {
        if (++i < argc) {
	  int c, num1=0, num2=0, num3=0;
	  char *tok;

	  /* el pr�ximo argumento debe indicar un formato de
	     resoluci�n algo como esto: 320x240[x8] o [8] */
	  c = 0;

	  for (tok=ustrtok(argv[i], "x"); tok;
	       tok=ustrtok(NULL, "x")) {
	    switch (c) {
	      case 0: num1 = ustrtol(tok, NULL, 10); break;
	      case 1: num2 = ustrtol(tok, NULL, 10); break;
	      case 2: num3 = ustrtol(tok, NULL, 10); break;
	    }
	    c++;
	  }

	  switch (c) {
	    case 1:
	      set_config_int("GfxMode", "Depth", num1);
	      break;
	    case 2:
	    case 3:
	      set_config_int("GfxMode", "Width", num1);
	      set_config_int("GfxMode", "Height", num2);
	      if (c == 3)
		set_config_int("GfxMode", "Depth", num3);
	      break;
	  }
	}
        else {
	  console.printf(_("%s: option \"res\" requires an argument\n"), 
			 m_pimpl->m_exe_name);
          usage(false);
	}
      }
      /* verbose mode */
      else if (strncmp(arg+n, "verbose", len) == 0) {
        ase_mode |= MODE_VERBOSE;
      }
      /* show help */
      else if (strncmp(arg+n, "help", len) == 0) {
        usage(true);
      }
      /* show version */
      else if (strncmp(arg+n, "version", len) == 0) {
        ase_mode |= MODE_BATCH;

        console.printf("ase %s\n", VERSION);
      }
      /* invalid argument */
      else {
        usage(false);
      }
    }
    /* graphic file to open */
    else if (n == 0)
      m_pimpl->m_options.push_back(new Option(Option::OpenSprite, argv[i]));
  }
}

/**
 * Shows the available options for the program
 */
void App::usage(bool show_help)
{
  Console console;

  ase_mode |= MODE_BATCH;
  if (!show_help)
    m_pimpl->m_return_code = 1;

  // show options
  if (show_help) {
    // copyright
    console.printf
      ("ase %s -- Allegro Sprite Editor, %s\n"
       COPYRIGHT "\n\n",
       VERSION, _("Just another tool to create sprites"));

    // usage
    console.printf
      ("%s\n  %s [%s] [%s]...\n\n",
       _("Usage:"), m_pimpl->m_exe_name, _("OPTION"), _("FILE"));

    /* options */
    console.printf
      ("%s:\n"
       "  -palette GFX-FILE        %s\n"
       "  -resolution WxH[xBPP]    %s\n"
       "  -verbose                 %s\n"
       "  -help                    %s\n"
       "  -version                 %s\n"
       "\n",
       _("Options"),
       _("Use a specific palette by default"),
       _("Change the resolution to use"),
       _("Explain what is being done (in stderr or a log file)"),
       _("Display this help and exits"),
       _("Output version information and exit"));

    /* web-site */
    console.printf
      ("%s: %s\n\n",
       _("Find more information in the ASE's official web site at:"), WEBSITE);
  }
  /* how to show options */
  else {
    console.printf(_("Try \"%s --help\" for more information.\n"), 
		   m_pimpl->m_exe_name);
  }
}
