/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include <Python.h>

#include "blender/CCL_api.h"

#include "blender/device.h"
#include "blender/session.h"
#include "blender/sync.h"
#include "blender/util.h"

#include "session/denoising.h"
#include "session/merge.h"

#include "util/debug.h"

#include "util/guiding.h"
#include "util/log.h"
#include "util/openimagedenoise.h"
#include "util/path.h"
#include "util/string.h"
#include "util/task.h"
#include "util/types.h"

#include "GPU_state.hh"

#include "scene/osl.h"

#ifdef WITH_METAL
#  include "device/metal/device.h"
#endif

CCL_NAMESPACE_BEGIN

namespace {

/* Flag describing whether debug flags were synchronized from scene. */
bool debug_flags_set = false;

void *pylong_as_voidptr_typesafe(PyObject *object)
{
  if (object == Py_None) {
    return nullptr;
  }
  return PyLong_AsVoidPtr(object);
}

PyObject *pyunicode_from_string(const char *str)
{
  /* Ignore errors if device API returns invalid UTF8 strings. */
  return PyUnicode_DecodeUTF8(str, strlen(str), "ignore");
}

/* Synchronize debug flags from a given Blender scene.
 * Return truth when device list needs invalidation.
 */
void debug_flags_sync_from_scene(BL::Scene b_scene)
{
  DebugFlagsRef flags = DebugFlags();
  PointerRNA cscene = RNA_pointer_get(&b_scene.ptr, "cycles");
  /* Synchronize CPU flags. */
  flags.cpu.avx2 = get_boolean(cscene, "debug_use_cpu_avx2");
  flags.cpu.sse42 = get_boolean(cscene, "debug_use_cpu_sse42");
  flags.cpu.bvh_layout = (BVHLayout)get_enum(cscene, "debug_bvh_layout");
  /* Synchronize CUDA flags. */
  flags.cuda.adaptive_compile = get_boolean(cscene, "debug_use_cuda_adaptive_compile");
  flags.hip.adaptive_compile = get_boolean(cscene, "debug_use_hip_adaptive_compile");
  flags.metal.adaptive_compile = get_boolean(cscene, "debug_use_metal_adaptive_compile");
  /* Synchronize OptiX flags. */
  flags.optix.use_debug = get_boolean(cscene, "debug_use_optix_debug");
}

/* Reset debug flags to default values.
 * Return truth when device list needs invalidation.
 */
void debug_flags_reset()
{
  DebugFlagsRef flags = DebugFlags();
  flags.reset();
}

} /* namespace */

void python_thread_state_save(void **python_thread_state)
{
  *python_thread_state = (void *)PyEval_SaveThread();
}

void python_thread_state_restore(void **python_thread_state)
{
  PyEval_RestoreThread((PyThreadState *)*python_thread_state);
  *python_thread_state = nullptr;
}

static const char *PyC_UnicodeAsBytes(PyObject *py_str, PyObject **coerce)
{
  const char *result = PyUnicode_AsUTF8(py_str);
  if (result) {
    /* 99% of the time this is enough but we better support non unicode
     * chars since blender doesn't limit this.
     */
    return result;
  }
  PyErr_Clear();
  if (PyBytes_Check(py_str)) {
    return PyBytes_AS_STRING(py_str);
  }

  *coerce = PyUnicode_EncodeFSDefault(py_str);
  if (*coerce) {
    return PyBytes_AS_STRING(*coerce);
  }
  /* Clear the error, so Cycles can be at least used without
   * GPU and OSL support,
   */
  PyErr_Clear();
  return "";
}

static PyObject *init_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *path;
  PyObject *user_path;
  int headless;

  if (!PyArg_ParseTuple(args, "OOi", &path, &user_path, &headless)) {
    return nullptr;
  }

  PyObject *path_coerce = nullptr;
  PyObject *user_path_coerce = nullptr;
  path_init(PyC_UnicodeAsBytes(path, &path_coerce),
            PyC_UnicodeAsBytes(user_path, &user_path_coerce));
  Py_XDECREF(path_coerce);
  Py_XDECREF(user_path_coerce);

  BlenderSession::headless = headless;

  Py_RETURN_NONE;
}

static PyObject *exit_func(PyObject * /*self*/, PyObject * /*args*/)
{
#ifdef WITH_METAL
  device_metal_exit();
#endif

  ColorSpaceManager::free_memory();
  OSLManager::free_memory();
  TaskScheduler::free_memory();
  Device::free_memory();
  Py_RETURN_NONE;
}

static PyObject *create_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pyengine;
  PyObject *pypreferences;
  PyObject *pydata;
  PyObject *pyscreen;
  PyObject *pyregion;
  PyObject *pyv3d;
  PyObject *pyrv3d;
  int preview_osl;

  if (!PyArg_ParseTuple(args,
                        "OOOOOOOi",
                        &pyengine,
                        &pypreferences,
                        &pydata,
                        &pyscreen,
                        &pyregion,
                        &pyv3d,
                        &pyrv3d,
                        &preview_osl))
  {
    return nullptr;
  }

  /* RNA */
  ID *bScreen = (ID *)PyLong_AsVoidPtr(pyscreen);

  const PointerRNA engineptr = RNA_pointer_create_discrete(
      nullptr, &RNA_RenderEngine, PyLong_AsVoidPtr(pyengine));
  BL::RenderEngine engine(engineptr);

  const PointerRNA preferencesptr = RNA_pointer_create_discrete(
      nullptr, &RNA_Preferences, PyLong_AsVoidPtr(pypreferences));
  BL::Preferences preferences(preferencesptr);

  const PointerRNA dataptr = RNA_main_pointer_create((Main *)PyLong_AsVoidPtr(pydata));
  BL::BlendData data(dataptr);

  const PointerRNA regionptr = RNA_pointer_create_discrete(
      bScreen, &RNA_Region, pylong_as_voidptr_typesafe(pyregion));
  BL::Region region(regionptr);

  const PointerRNA v3dptr = RNA_pointer_create_discrete(
      bScreen, &RNA_SpaceView3D, pylong_as_voidptr_typesafe(pyv3d));
  BL::SpaceView3D v3d(v3dptr);

  const PointerRNA rv3dptr = RNA_pointer_create_discrete(
      bScreen, &RNA_RegionView3D, pylong_as_voidptr_typesafe(pyrv3d));
  BL::RegionView3D rv3d(rv3dptr);

  /* create session */
  BlenderSession *session;

  if (rv3d) {
    /* interactive viewport session */
    const int width = region.width();
    const int height = region.height();

    session = new BlenderSession(engine, preferences, data, v3d, rv3d, width, height);
  }
  else {
    /* offline session or preview render */
    session = new BlenderSession(engine, preferences, data, preview_osl);
  }

  return PyLong_FromVoidPtr(session);
}

static PyObject *free_func(PyObject * /*self*/, PyObject *value)
{
  delete (BlenderSession *)PyLong_AsVoidPtr(value);

  Py_RETURN_NONE;
}

static PyObject *render_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pysession;
  PyObject *pydepsgraph;

  if (!PyArg_ParseTuple(args, "OO", &pysession, &pydepsgraph)) {
    return nullptr;
  }

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  const PointerRNA depsgraphptr = RNA_pointer_create_discrete(
      nullptr, &RNA_Depsgraph, (ID *)PyLong_AsVoidPtr(pydepsgraph));
  BL::Depsgraph b_depsgraph(depsgraphptr);

  /* Allow Blender to execute other Python scripts. */
  python_thread_state_save(&session->python_thread_state);

  session->render(b_depsgraph);

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *render_frame_finish_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pysession;

  if (!PyArg_ParseTuple(args, "O", &pysession)) {
    return nullptr;
  }

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  /* Allow Blender to execute other Python scripts. */
  python_thread_state_save(&session->python_thread_state);

  session->render_frame_finish();

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *draw_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *py_session;
  PyObject *py_graph;
  PyObject *py_screen;
  PyObject *py_space_image;

  if (!PyArg_ParseTuple(args, "OOOO", &py_session, &py_graph, &py_screen, &py_space_image)) {
    return nullptr;
  }

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(py_session);

  ID *b_screen = (ID *)PyLong_AsVoidPtr(py_screen);

  const PointerRNA b_space_image_ptr = RNA_pointer_create_discrete(
      b_screen, &RNA_SpaceImageEditor, pylong_as_voidptr_typesafe(py_space_image));
  BL::SpaceImageEditor b_space_image(b_space_image_ptr);

  session->draw(b_space_image);

  Py_RETURN_NONE;
}

/* pixel_array and result passed as pointers */
static PyObject *bake_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pysession;
  PyObject *pydepsgraph;
  PyObject *pyobject;
  const char *pass_type;
  int pass_filter;
  int width;
  int height;

  if (!PyArg_ParseTuple(args,
                        "OOOsiii",
                        &pysession,
                        &pydepsgraph,
                        &pyobject,
                        &pass_type,
                        &pass_filter,
                        &width,
                        &height))
  {
    return nullptr;
  }

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  const PointerRNA depsgraphptr = RNA_pointer_create_discrete(
      nullptr, &RNA_Depsgraph, PyLong_AsVoidPtr(pydepsgraph));
  BL::Depsgraph b_depsgraph(depsgraphptr);

  const PointerRNA objectptr = RNA_id_pointer_create((ID *)PyLong_AsVoidPtr(pyobject));
  BL::Object b_object(objectptr);

  python_thread_state_save(&session->python_thread_state);

  session->bake(b_depsgraph, b_object, pass_type, pass_filter, width, height);

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *view_draw_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pysession;
  PyObject *pygraph;
  PyObject *pyv3d;
  PyObject *pyrv3d;

  if (!PyArg_ParseTuple(args, "OOOO", &pysession, &pygraph, &pyv3d, &pyrv3d)) {
    return nullptr;
  }

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  if (PyLong_AsVoidPtr(pyrv3d)) {
    /* 3d view drawing */
    int viewport[4];
    GPU_viewport_size_get_i(viewport);

    session->view_draw(viewport[2], viewport[3]);
  }

  Py_RETURN_NONE;
}

static PyObject *reset_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pysession;
  PyObject *pydata;
  PyObject *pydepsgraph;

  if (!PyArg_ParseTuple(args, "OOO", &pysession, &pydata, &pydepsgraph)) {
    return nullptr;
  }

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  const PointerRNA dataptr = RNA_main_pointer_create((Main *)PyLong_AsVoidPtr(pydata));
  BL::BlendData b_data(dataptr);

  const PointerRNA depsgraphptr = RNA_pointer_create_discrete(
      nullptr, &RNA_Depsgraph, PyLong_AsVoidPtr(pydepsgraph));
  BL::Depsgraph b_depsgraph(depsgraphptr);

  python_thread_state_save(&session->python_thread_state);

  session->reset_session(b_data, b_depsgraph);

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *sync_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pysession;
  PyObject *pydepsgraph;

  if (!PyArg_ParseTuple(args, "OO", &pysession, &pydepsgraph)) {
    return nullptr;
  }

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  const PointerRNA depsgraphptr = RNA_pointer_create_discrete(
      nullptr, &RNA_Depsgraph, PyLong_AsVoidPtr(pydepsgraph));
  BL::Depsgraph b_depsgraph(depsgraphptr);

  python_thread_state_save(&session->python_thread_state);

  session->synchronize(b_depsgraph);

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *available_devices_func(PyObject * /*self*/, PyObject *args)
{
  const char *type_name;
  if (!PyArg_ParseTuple(args, "s", &type_name)) {
    return nullptr;
  }

  const DeviceType type = Device::type_from_string(type_name);
  /* "NONE" is defined by the add-on, see: `CyclesPreferences.get_device_types`. */
  if ((type == DEVICE_NONE) && (strcmp(type_name, "NONE") != 0)) {
    PyErr_Format(PyExc_ValueError, "Device \"%s\" not known.", type_name);
    return nullptr;
  }

  uint mask = (type == DEVICE_NONE) ? DEVICE_MASK_ALL : DEVICE_MASK(type);
  mask |= DEVICE_MASK_CPU;

  vector<DeviceInfo> devices = Device::available_devices(mask);
  PyObject *ret = PyTuple_New(devices.size());

  for (size_t i = 0; i < devices.size(); i++) {
    const DeviceInfo &device = devices[i];
    const string type_name = Device::string_from_type(device.type);
    PyObject *device_tuple = PyTuple_New(8);
    PyTuple_SET_ITEM(device_tuple, 0, pyunicode_from_string(device.description.c_str()));
    PyTuple_SET_ITEM(device_tuple, 1, pyunicode_from_string(type_name.c_str()));
    PyTuple_SET_ITEM(device_tuple, 2, pyunicode_from_string(device.id.c_str()));
    PyTuple_SET_ITEM(device_tuple, 3, PyBool_FromLong(device.has_peer_memory));
    PyTuple_SET_ITEM(device_tuple, 4, PyBool_FromLong(device.use_hardware_raytracing));
    PyTuple_SET_ITEM(
        device_tuple, 5, PyBool_FromLong(device.denoisers & DENOISER_OPENIMAGEDENOISE));
    PyTuple_SET_ITEM(device_tuple, 6, PyBool_FromLong(device.denoisers & DENOISER_OPTIX));
    PyTuple_SET_ITEM(device_tuple, 7, PyBool_FromLong(device.has_execution_optimization));
    PyTuple_SET_ITEM(ret, i, device_tuple);
  }

  return ret;
}

#ifdef WITH_OSL

static PyObject *osl_compile_func(PyObject * /*self*/, PyObject *args)
{
  const char *inputfile = nullptr;
  const char *outputfile = nullptr;

  if (!PyArg_ParseTuple(args, "ss", &inputfile, &outputfile)) {
    return nullptr;
  }

  /* return */
  if (!OSLManager::osl_compile(inputfile, outputfile)) {
    Py_RETURN_FALSE;
  }

  Py_RETURN_TRUE;
}
#endif

static PyObject *system_info_func(PyObject * /*self*/, PyObject * /*value*/)
{
  const string system_info = Device::device_capabilities();
  return pyunicode_from_string(system_info.c_str());
}

static bool image_parse_filepaths(PyObject *pyfilepaths, vector<string> &filepaths)
{
  if (PyUnicode_Check(pyfilepaths)) {
    const char *filepath = PyUnicode_AsUTF8(pyfilepaths);
    filepaths.push_back(filepath);
    return true;
  }

  PyObject *sequence = PySequence_Fast(pyfilepaths,
                                       "File paths must be a string or sequence of strings");
  if (sequence == nullptr) {
    return false;
  }

  for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(sequence); i++) {
    PyObject *item = PySequence_Fast_GET_ITEM(sequence, i);
    const char *filepath = PyUnicode_AsUTF8(item);
    if (filepath == nullptr) {
      PyErr_SetString(PyExc_ValueError, "File paths must be a string or sequence of strings.");
      Py_DECREF(sequence);
      return false;
    }
    filepaths.push_back(filepath);
  }
  Py_DECREF(sequence);

  return true;
}

static PyObject *denoise_func(PyObject * /*self*/, PyObject *args, PyObject *keywords)
{
  static const char *keyword_list[] = {
      "preferences", "scene", "view_layer", "input", "output", nullptr};
  PyObject *pypreferences;
  PyObject *pyscene;
  PyObject *pyviewlayer;
  PyObject *pyinput;
  PyObject *pyoutput = nullptr;

  if (!PyArg_ParseTupleAndKeywords(args,
                                   keywords,
                                   "OOOO|O",
                                   (char **)keyword_list,
                                   &pypreferences,
                                   &pyscene,
                                   &pyviewlayer,
                                   &pyinput,
                                   &pyoutput))
  {
    return nullptr;
  }

  /* Get device specification from preferences and scene. */
  const PointerRNA preferencesptr = RNA_pointer_create_discrete(
      nullptr, &RNA_Preferences, PyLong_AsVoidPtr(pypreferences));
  BL::Preferences b_preferences(preferencesptr);

  const PointerRNA sceneptr = RNA_id_pointer_create((ID *)PyLong_AsVoidPtr(pyscene));
  BL::Scene b_scene(sceneptr);

  DeviceInfo preferences_device;
  const DeviceInfo pathtrace_device = blender_device_info(
      b_preferences, b_scene, true, true, preferences_device);

  /* Get denoising parameters from view layer. */
  const PointerRNA viewlayerptr = RNA_pointer_create_discrete(
      (ID *)PyLong_AsVoidPtr(pyscene), &RNA_ViewLayer, PyLong_AsVoidPtr(pyviewlayer));
  BL::ViewLayer b_view_layer(viewlayerptr);

  DenoiseParams params = BlenderSync::get_denoise_params(
      b_scene, b_view_layer, true, preferences_device);
  params.use = true;

  /* Parse file paths list. */
  vector<string> input;
  vector<string> output;

  if (!image_parse_filepaths(pyinput, input)) {
    return nullptr;
  }

  if (pyoutput) {
    if (!image_parse_filepaths(pyoutput, output)) {
      return nullptr;
    }
  }
  else {
    output = input;
  }

  if (input.empty()) {
    PyErr_SetString(PyExc_ValueError, "No input file paths specified.");
    return nullptr;
  }
  if (input.size() != output.size()) {
    PyErr_SetString(PyExc_ValueError, "Number of input and output file paths does not match.");
    return nullptr;
  }

  /* Create denoiser. */
  /* We are using preference device here, because path trace device will be identical to it unless
   * scene is setting CPU render or command line override render device. But both of this options
   * are for render, not for denoising. */
  DenoiserPipeline denoiser(preferences_device, params);
  denoiser.input = input;
  denoiser.output = output;

  /* Run denoiser. */
  if (!denoiser.run()) {
    PyErr_SetString(PyExc_ValueError, denoiser.error.c_str());
    return nullptr;
  }

  Py_RETURN_NONE;
}

static PyObject *merge_func(PyObject * /*self*/, PyObject *args, PyObject *keywords)
{
  static const char *keyword_list[] = {"input", "output", nullptr};
  PyObject *pyinput;
  PyObject *pyoutput = nullptr;

  if (!PyArg_ParseTupleAndKeywords(
          args, keywords, "OO", (char **)keyword_list, &pyinput, &pyoutput))
  {
    return nullptr;
  }

  /* Parse input list. */
  vector<string> input;
  if (!image_parse_filepaths(pyinput, input)) {
    return nullptr;
  }

  /* Parse output string. */
  if (!PyUnicode_Check(pyoutput)) {
    PyErr_SetString(PyExc_ValueError, "Output must be a string.");
    return nullptr;
  }
  const string output = PyUnicode_AsUTF8(pyoutput);

  /* Merge. */
  ImageMerger merger;
  merger.input = input;
  merger.output = output;

  if (!merger.run()) {
    PyErr_SetString(PyExc_ValueError, merger.error.c_str());
    return nullptr;
  }

  Py_RETURN_NONE;
}

static PyObject *debug_flags_update_func(PyObject * /*self*/, PyObject *args)
{
  PyObject *pyscene;
  if (!PyArg_ParseTuple(args, "O", &pyscene)) {
    return nullptr;
  }

  const PointerRNA sceneptr = RNA_id_pointer_create((ID *)PyLong_AsVoidPtr(pyscene));
  const BL::Scene b_scene(sceneptr);

  debug_flags_sync_from_scene(b_scene);

  debug_flags_set = true;

  Py_RETURN_NONE;
}

static PyObject *debug_flags_reset_func(PyObject * /*self*/, PyObject * /*args*/)
{
  debug_flags_reset();
  if (debug_flags_set) {
    debug_flags_set = false;
  }
  Py_RETURN_NONE;
}

static PyObject *enable_print_stats_func(PyObject * /*self*/, PyObject * /*args*/)
{
  BlenderSession::print_render_stats = true;
  Py_RETURN_NONE;
}

static PyObject *get_device_types_func(PyObject * /*self*/, PyObject * /*args*/)
{
  const vector<DeviceType> device_types = Device::available_types();
  bool has_cuda = false;
  bool has_optix = false;
  bool has_hip = false;
  bool has_metal = false;
  bool has_oneapi = false;
  bool has_hiprt = false;
  for (const DeviceType device_type : device_types) {
    has_cuda |= (device_type == DEVICE_CUDA);
    has_optix |= (device_type == DEVICE_OPTIX);
    has_hip |= (device_type == DEVICE_HIP);
    has_metal |= (device_type == DEVICE_METAL);
    has_oneapi |= (device_type == DEVICE_ONEAPI);
    has_hiprt |= (device_type == DEVICE_HIPRT);
  }
  PyObject *list = PyTuple_New(6);
  PyTuple_SET_ITEM(list, 0, PyBool_FromLong(has_cuda));
  PyTuple_SET_ITEM(list, 1, PyBool_FromLong(has_optix));
  PyTuple_SET_ITEM(list, 2, PyBool_FromLong(has_hip));
  PyTuple_SET_ITEM(list, 3, PyBool_FromLong(has_metal));
  PyTuple_SET_ITEM(list, 4, PyBool_FromLong(has_oneapi));
  PyTuple_SET_ITEM(list, 5, PyBool_FromLong(has_hiprt));
  return list;
}

static PyObject *set_device_override_func(PyObject * /*self*/, PyObject *arg)
{
  PyObject *override_string = PyObject_Str(arg);
  string override = PyUnicode_AsUTF8(override_string);
  Py_DECREF(override_string);

  bool include_cpu = false;
  const string cpu_suffix = "+CPU";
  if (string_endswith(override, cpu_suffix)) {
    include_cpu = true;
    override = override.substr(0, override.length() - cpu_suffix.length());
  }

  if (override == "CPU") {
    BlenderSession::device_override = DEVICE_MASK_CPU;
  }
  else if (override == "CUDA") {
    BlenderSession::device_override = DEVICE_MASK_CUDA;
  }
  else if (override == "OPTIX") {
    BlenderSession::device_override = DEVICE_MASK_OPTIX;
  }
  else if (override == "HIP") {
    BlenderSession::device_override = DEVICE_MASK_HIP;
  }
  else if (override == "METAL") {
    BlenderSession::device_override = DEVICE_MASK_METAL;
  }
  else if (override == "ONEAPI") {
    BlenderSession::device_override = DEVICE_MASK_ONEAPI;
  }
  else {
    LOG_ERROR << override << " is not a valid Cycles device.";
    Py_RETURN_FALSE;
  }

  if (include_cpu) {
    BlenderSession::device_override = (DeviceTypeMask)(BlenderSession::device_override |
                                                       DEVICE_MASK_CPU);
  }

  Py_RETURN_TRUE;
}

#ifdef __GNUC__
#  ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wcast-function-type"
#  else
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wcast-function-type"
#  endif
#endif

static PyMethodDef methods[] = {
    {"init", init_func, METH_VARARGS, ""},
    {"exit", exit_func, METH_VARARGS, ""},
    {"create", create_func, METH_VARARGS, ""},
    {"free", free_func, METH_O, ""},
    {"render", render_func, METH_VARARGS, ""},
    {"render_frame_finish", render_frame_finish_func, METH_VARARGS, ""},
    {"draw", draw_func, METH_VARARGS, ""},
    {"bake", bake_func, METH_VARARGS, ""},
    {"view_draw", view_draw_func, METH_VARARGS, ""},
    {"sync", sync_func, METH_VARARGS, ""},
    {"reset", reset_func, METH_VARARGS, ""},
#ifdef WITH_OSL
    {"osl_compile", osl_compile_func, METH_VARARGS, ""},
#endif
    {"available_devices", available_devices_func, METH_VARARGS, ""},
    {"system_info", system_info_func, METH_NOARGS, ""},

    /* Standalone denoising */
    {"denoise", (PyCFunction)denoise_func, METH_VARARGS | METH_KEYWORDS, ""},
    {"merge", (PyCFunction)merge_func, METH_VARARGS | METH_KEYWORDS, ""},

    /* Debugging routines */
    {"debug_flags_update", debug_flags_update_func, METH_VARARGS, ""},
    {"debug_flags_reset", debug_flags_reset_func, METH_NOARGS, ""},

    /* Statistics. */
    {"enable_print_stats", enable_print_stats_func, METH_NOARGS, ""},

    /* Compute Device selection */
    {"get_device_types", get_device_types_func, METH_VARARGS, ""},
    {"set_device_override", set_device_override_func, METH_O, ""},

    {nullptr, nullptr, 0, nullptr},
};

#ifdef __GNUC__
#  ifdef __clang__
#    pragma clang diagnostic pop
#  else
#    pragma GCC diagnostic pop
#  endif
#endif

static struct PyModuleDef module = {
    /*m_base*/ PyModuleDef_HEAD_INIT,
    /*m_name*/ "_cycles",
    /*m_doc*/ "Blender cycles render integration",
    /*m_size*/ -1,
    /*m_methods*/ methods,
    /*m_slots*/ nullptr,
    /*m_traverse*/ nullptr,
    /*m_clear*/ nullptr,
    /*m_free*/ nullptr,
};

CCL_NAMESPACE_END

void *CCL_python_module_init()
{
  PyObject *mod = PyModule_Create(&ccl::module);

#ifdef WITH_OSL
  /* TODO(sergey): This gives us library we've been linking against.
   *               In theory with dynamic OSL library it might not be
   *               accurate, but there's nothing in OSL API which we
   *               might use to get version in runtime.
   */
  const int curversion = OSL_LIBRARY_VERSION_CODE;
  PyModule_AddObjectRef(mod, "with_osl", Py_True);
  PyModule_AddObject(
      mod,
      "osl_version",
      Py_BuildValue("(iii)", curversion / 10000, (curversion / 100) % 100, curversion % 100));
  PyModule_AddObject(
      mod,
      "osl_version_string",
      PyUnicode_FromFormat(
          "%2d, %2d, %2d", curversion / 10000, (curversion / 100) % 100, curversion % 100));
#else
  PyModule_AddObjectRef(mod, "with_osl", Py_False);
  PyModule_AddStringConstant(mod, "osl_version", "unknown");
  PyModule_AddStringConstant(mod, "osl_version_string", "unknown");
#endif

  if (ccl::guiding_supported()) {
    PyModule_AddObjectRef(mod, "with_path_guiding", Py_True);
  }
  else {
    PyModule_AddObjectRef(mod, "with_path_guiding", Py_False);
  }

#ifdef WITH_EMBREE
  PyModule_AddObjectRef(mod, "with_embree", Py_True);
#else  /* WITH_EMBREE */
  PyModule_AddObjectRef(mod, "with_embree", Py_False);
#endif /* WITH_EMBREE */

#ifdef WITH_EMBREE_GPU
  PyModule_AddObjectRef(mod, "with_embree_gpu", Py_True);
#else  /* WITH_EMBREE_GPU */
  PyModule_AddObjectRef(mod, "with_embree_gpu", Py_False);
#endif /* WITH_EMBREE_GPU */

  if (ccl::openimagedenoise_supported()) {
    PyModule_AddObjectRef(mod, "with_openimagedenoise", Py_True);
  }
  else {
    PyModule_AddObjectRef(mod, "with_openimagedenoise", Py_False);
  }

#ifdef WITH_CYCLES_DEBUG
  PyModule_AddObjectRef(mod, "with_debug", Py_True);
#else  /* WITH_CYCLES_DEBUG */
  PyModule_AddObjectRef(mod, "with_debug", Py_False);
#endif /* WITH_CYCLES_DEBUG */

  return (void *)mod;
}
