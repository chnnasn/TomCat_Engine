#include "tcpch.h"
#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebWindow.h"
#include "TomCat/Core/Input.h"
#include "TomCat/Events/ApplicationEvent.h"
#include <GLFW/glfw3.h>
#include <emscripten/html5.h>
#include <stdexcept>

namespace TomCat {
WebWindow::WebWindow(const WindowProps& props) : m_Width(props.Width), m_Height(props.Height) {
  if (!glfwInit()) throw std::runtime_error("GLFW browser initialization failed");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  m_Window = glfwCreateWindow(m_Width, m_Height, props.Title.c_str(), nullptr, nullptr);
  if (!m_Window) { glfwTerminate(); throw std::runtime_error("WebGL2 context creation failed"); }
  glfwMakeContextCurrent(m_Window);
  glfwSetWindowUserPointer(m_Window, this);
  glfwSetKeyCallback(m_Window, [](GLFWwindow*, int key, int, int action, int) {
    if (key < 0) return;
    Input::NotifyKey(key, action == GLFW_RELEASE ? InputEventQueue::Action::Released :
      action == GLFW_REPEAT ? InputEventQueue::Action::Repeated : InputEventQueue::Action::Pressed, glfwGetTime());
  });
  glfwSetMouseButtonCallback(m_Window, [](GLFWwindow*, int button, int action, int) {
    Input::NotifyMouseButton(button, action == GLFW_RELEASE ? InputEventQueue::Action::Released : InputEventQueue::Action::Pressed, glfwGetTime());
  });
  glfwSetCursorPosCallback(m_Window, [](GLFWwindow*, double x, double y) { Input::NotifyMousePosition(x, y); });
  glfwSetScrollCallback(m_Window, [](GLFWwindow*, double x, double y) { Input::NotifyScroll(x, y); });
  glfwSetWindowFocusCallback(m_Window, [](GLFWwindow*, int focused) { Input::NotifyWindowFocus(focused != 0, glfwGetTime()); });
  glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
    auto& self = *static_cast<WebWindow*>(glfwGetWindowUserPointer(window));
    self.m_Width = width; self.m_Height = height;
    WindowResizeEvent event(width, height);
    if (self.m_Callback) self.m_Callback(event);
  });
}
WebWindow::~WebWindow() { if (m_Window) glfwDestroyWindow(m_Window); glfwTerminate(); Input::ClearState(); }
void WebWindow::PollEvents() { glfwPollEvents(); }
void WebWindow::Present() { glfwSwapBuffers(m_Window); }
void WebWindow::SetTitle(const std::string& title) { glfwSetWindowTitle(m_Window, title.c_str()); }
double WebWindow::GetTimeSeconds() const { return glfwGetTime(); }
void WebWindow::CancelCloseRequest() { glfwSetWindowShouldClose(m_Window, GLFW_FALSE); }
void WebWindow::Resize(uint32_t width, uint32_t height) {
  glfwSetWindowSize(m_Window, width, height);
  m_Width = width; m_Height = height;
  WindowResizeEvent event(width, height);
  if (m_Callback) m_Callback(event);
}
}
#endif // __EMSCRIPTEN__
