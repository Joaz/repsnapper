// LGPLv2+ ...

#include "config.h"
#include "stdafx.h"
#include "view.h"
#include "arcball.h"
#include "gllight.h"
#include "processcontroller.h"

#define N_LIGHTS (sizeof (m_lights) / sizeof(m_lights[0]))

View::View(ProcessController &pc) :
  m_arcBall( new ArcBall() ), m_pc( pc )
{
  set_events (Gdk::POINTER_MOTION_MASK |
	      Gdk::BUTTON_MOTION_MASK |
	      Gdk::BUTTON_PRESS_MASK |
	      Gdk::BUTTON1_MOTION_MASK |
	      Gdk::BUTTON2_MOTION_MASK |
	      Gdk::BUTTON3_MOTION_MASK);

  Glib::RefPtr<Gdk::GL::Config> glconfig;

  glconfig = Gdk::GL::Config::create(Gdk::GL::MODE_RGB    |
                                     Gdk::GL::MODE_DEPTH  |
                                     Gdk::GL::MODE_DOUBLE);
  if (!glconfig) // try single buffered
    glconfig = Gdk::GL::Config::create(Gdk::GL::MODE_RGB   |
				       Gdk::GL::MODE_DEPTH);
  set_gl_capability(glconfig);

  memset (&m_transform.M, 0, sizeof (m_transform.M));

  Matrix3fT identity;
  Matrix3fSetIdentity(&identity);
  Matrix4fSetRotationScaleFromMatrix3f(&m_transform, &identity);
  m_transform.s.SW = 1.0;

  m_zoom = 100.0f;
  for (uint i = 0; i < N_LIGHTS; i++)
    m_lights[i] = NULL;

  m_pc.signal_rfo_changed().connect (sigc::mem_fun(*this, &View::rfo_changed));
}

View::~View()
{
  delete m_arcBall;
}

void View::rfo_changed()
{
  if (!m_pc.rfo.Objects.size())
    return;

  m_max = Vector3f(G_MINFLOAT, G_MINFLOAT, G_MINFLOAT);
  m_min = Vector3f(G_MAXFLOAT, G_MAXFLOAT, G_MAXFLOAT);

  for (uint i = 0 ; i < m_pc.rfo.Objects.size(); i++) {
    for (uint j = 0; j < m_pc.rfo.Objects[i].files.size(); j++) {
      Matrix4f M = m_pc.GetSTLTransformationMatrix (i, j);
      Vector3f stlMin = M * m_pc.rfo.Objects[i].files[j].stl.Min;
      Vector3f stlMax = M * m_pc.rfo.Objects[i].files[j].stl.Max;
      for (uint k = 0; k < 3; k++) {
	m_min.xyz[k] = MIN(stlMin.xyz[k], m_min.xyz[k]);
	m_max.xyz[k] = MAX(stlMax.xyz[k], m_max.xyz[k]);
      }
    }
  }

  m_center = (m_max - m_min) / 2.0;
  m_zoom = (m_max - m_min).getMaxComponent();
}

bool View::on_configure_event(GdkEventConfigure* event)
{
  Glib::RefPtr<Gdk::GL::Drawable> gldrawable = get_gl_drawable();
  if (!gldrawable->gl_begin(get_gl_context()))
    return false;

  glLoadIdentity();
  glViewport (0, 0, get_width(), get_height());
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  gluPerspective (45.0f, (float)get_width()/(float)get_height(),1.0f, 1000000.0f);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  m_arcBall->setBounds(get_width(), get_height());
  glEnable(GL_LIGHTING);

  struct { GLfloat x; GLfloat y; GLfloat z; } light_locations[] = {
    { -100,  100, 200 },
    {  100,  100, 200 },
    {  100, -100, 200 },
    {  100, -100, 200 }
  };
  for (uint i = 0; i < N_LIGHTS; i++) {
    delete (m_lights[i]);
    m_lights[i] = new gllight();
    m_lights[i]->Init((GLenum)(GL_LIGHT0+i));
    m_lights[i]->SetAmbient(0.2f, 0.2f, 0.2f, 1.0f);
    m_lights[i]->SetDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
    m_lights[i]->SetSpecular(1.0f, 1.0f, 1.0f, 1.0f);
    m_lights[i]->Enable(false);
    m_lights[i]->SetLocation(light_locations[i].x,
			   light_locations[i].y,
			   light_locations[i].z, 0);
  }
  m_lights[0]->Enable(true);
  m_lights[3]->Enable(true);

  glDisable ( GL_LIGHTING);
  glDepthFunc (GL_LEQUAL);
  glEnable (GL_DEPTH_TEST);
  glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  m_quadratic = gluNewQuadric();
  gluQuadricNormals(m_quadratic, GLU_SMOOTH);
  gluQuadricTexture(m_quadratic, GL_TRUE);

  return true;
}

bool View::on_expose_event(GdkEventExpose* event)
{
  Glib::RefPtr<Gdk::GL::Drawable> gldrawable = get_gl_drawable();
  if (!gldrawable->gl_begin(get_gl_context()))
    return false;

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glLoadIdentity();
  glTranslatef (0.0, 0.0, -2.0 * m_zoom);
  glMultMatrixf (m_transform.M);
  CenterView();
  glPushMatrix();
  glColor3f(0.75f,0.75f,1.0f);

  glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, 1);
  //  Flu_Tree_Browser::Node *node = gui->RFP_Browser->get_selected( 1 );
  m_pc.Draw(NULL);

  glPopMatrix();

  if (gldrawable->is_double_buffered())
    gldrawable->swap_buffers();
  else
    glFlush();

    return true;
}

bool View::on_button_press_event(GdkEventButton* event)
{
  if (event->button == 1)
    m_arcBall->click (event->x, event->y);
  else if (event->button == 3)
    m_downPoint = Vector2f (event->x, event->y);
  else
    return Gtk::DrawingArea::on_button_press_event (event);
  return true;
}

bool View::on_scroll_event(GdkEventScroll* event)
{
  double factor = 110.0/100.0;
  if (event->direction == GDK_SCROLL_UP)
    m_zoom /= factor;
  else
    m_zoom *= factor;
  queue_draw();
  return true;
}

bool View::on_motion_notify_event(GdkEventMotion* event)
{
  if (event->state & GDK_BUTTON1_MASK) {
    m_arcBall->dragAccumulate(event->x, event->y, &m_transform);
    queue_draw();
    return true;
  }
  else if (event->state & GDK_BUTTON3_MASK) {
    Vector2f dragp(event->x, event->y);
    Vector2f delta = m_downPoint - dragp;
    m_downPoint = dragp;

    Matrix4f matrix;
    memcpy(&matrix.m00, &m_transform.M[0], sizeof(Matrix4f));
    Vector3f X(delta.x,0,0);
    X = matrix * X;
    Vector3f Y(0,-delta.y,0);
    Y = matrix * Y;
    m_center += X*delta.length()*0.01f;
    m_center += Y*delta.length()*0.01f;
    queue_draw();
    return true;
  }
  return Gtk::DrawingArea::on_motion_notify_event (event);
}

void View::SetEnableLight(unsigned int i, bool on)
{
  assert (i < N_LIGHTS);
  m_lights[i]->Enable(on);
  queue_draw();
}

void View::CenterView()
{
  glTranslatef (-m_center.x - m_pc.printOffset.x,
		-m_center.y - m_pc.printOffset.y,
		-m_center.z);
}
