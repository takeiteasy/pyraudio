/* pal.c -- https://github.com/takeiteasy/pal
 
 Copyright 2025 George Watson
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE. */

#include <Python.h>
#include "raudio.h"

typedef struct {
    PyObject_HEAD
    Wave wave;
} PalWave;

static int PalWave_Init(PalWave *self, PyObject *args, PyObject *kwds) {
    char *path = NULL;  // Initialize to NULL
    static char *kwlist[] = {"path", NULL}; // Keyword arguments
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path))
        return -1; // Error parsing arguments
    if (path) {
        PyObject *path_obj = PyUnicode_FromString(path);
        if (PyObject_SetAttrString((PyObject*)self, "path", path_obj) < 0) {
            Py_DECREF(path_obj);
            return -1;
        }
        Py_DECREF(path_obj);
        self->wave = LoadWave(path);
        if (!IsWaveReady(self->wave))
            return -1;
    }
    return 0;
}

static PyObject* PalWave_New(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PalWave* self = (PalWave*)type->tp_alloc(type, 0);
    if (!self)
        return PyErr_NoMemory();
    memset(&self->wave, 0, sizeof(Wave));
    return (PyObject*)self;
}

static void PalWave_Dealloc(PalWave* self) {
    if (IsWaveReady(self->wave))
        UnloadWave(self->wave);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static Py_ssize_t PalWave_length(PalWave *self) {
    float seconds = (float)self->wave.frameCount*1000.0/(self->wave.sampleRate*self->wave.channels);
    return (Py_ssize_t)seconds;
}

static PyMappingMethods PalWave_mapping = {
    (lenfunc)PalWave_length, NULL, NULL
};

static PyObject* pywave_is_ready(PalWave *self, PyObject *args) {
    if (IsWaveReady(self->wave)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* pywave_export(PalWave *self, PyObject *args, PyObject *kwds) {
    char *path = NULL;  // Initialize to NULL
    static char *kwlist[] = {"path", NULL}; // Keyword arguments
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path)) {
        PyErr_SetString(PyExc_RuntimeError, "No path passed to export");
        return NULL; // Error parsing arguments
    }
    if (!ExportWave(self->wave, path)) {
        PyErr_Format(PyExc_OSError, "Failed to write to file \"%s\"", path);
        return NULL;
    }
    Py_RETURN_TRUE;
}

static PyObject* pywave_copy(PalWave *self, PyObject *args, PyObject *kwds) {
    if (!IsWaveReady(self->wave)) {
        PyErr_SetString(PyExc_RuntimeError, "Cannot copy uninitialized Wave");
        return NULL;
    }
    PalWave* copy = (PalWave*)self->ob_base.ob_type->tp_alloc(self->ob_base.ob_type, 0);
    if (!copy)
        return PyErr_NoMemory();
    memset(&copy->wave, 0, sizeof(Wave));
    copy->wave = WaveCopy(self->wave);
    return (PyObject*)copy;
}

static int PalWave_getbuffer(PyObject *obj, Py_buffer *view, int flags) {
    if (view == NULL) {
        PyErr_SetString(PyExc_ValueError, "NULL view in getbuffer");
        return -1;
    }
    PalWave *self = (PalWave*)obj;
    view->obj = (PyObject*)self;
    view->buf = self->wave.data;
    view->len = self->wave.frameCount * self->wave.channels * sizeof(short);
    view->readonly = 0;
    view->itemsize = sizeof(short);
    view->format = "B";
    view->ndim = 1;
    view->shape = NULL;
    view->strides = NULL;
    view->suboffsets = NULL;
    view->internal = NULL;
    Py_INCREF(self);
    return 0;
}

static PyBufferProcs PalWave_as_buffer = {
  (getbufferproc)PalWave_getbuffer,
  (releasebufferproc)0,
};

static PyObject* pywave_setpointer(PalWave *self, PyObject *original) {
    Py_buffer buffer;
    if (PyObject_GetBuffer(original, &buffer, PyBUF_WRITABLE) == -1)
        return NULL;
    self->wave.data = buffer.buf;
    self->wave.frameCount = buffer.len / (self->wave.channels * sizeof(short));
    PyBuffer_Release(&buffer);
    return Py_BuildValue("d", 1);
}

static PyObject* pywave_samples(PalWave *self, PyObject *args) {
    float *samples = LoadWaveSamples(self->wave);
    if (!samples)
        return PyErr_NoMemory();
    int length = self->wave.frameCount * self->wave.channels;
    PyObject *list = PyList_New(length);  // Create a new Python list
    if (list == NULL) {
        free(samples);
        return PyErr_NoMemory();
    }
    
    for (int i = 0; i < length; i++) {
        PyObject *float_obj = PyFloat_FromDouble((double)samples[i]);
        if (float_obj == NULL) {
            for (int j = 0; j < i; j++)
                Py_DECREF(PyList_GetItem(list,j));
            Py_DECREF(list);
            free(samples);
            return NULL;
        }
        int result = PyList_SetItem(list, i, float_obj);
        if (result != 0) {
            Py_DECREF(float_obj);
            for (int j = 0; j <= i; j++)
                Py_DECREF(PyList_GetItem(list,j));
            Py_DECREF(list);
            free(samples);
            return NULL;
        }
    }
    UnloadWaveSamples(samples);
    return list;
}

static PyObject* pywave_crop(PalWave *self, PyObject *args) {
    unsigned int initSample, finalSample;
    if (!PyArg_ParseTuple(args, "ii", &initSample, &finalSample)) {
        return NULL; // Error parsing arguments (exception already set)
    }
    if (initSample < 0 || finalSample < 0 || initSample >= finalSample ||
        initSample >= self->wave.frameCount || finalSample > self->wave.frameCount) {
        PyErr_SetString(PyExc_ValueError, "Invalid sample range for crop");
        return NULL;
    }
    WaveCrop(&self->wave, initSample, finalSample);
    Py_RETURN_NONE;
}

static PyObject* pywave_format(PalWave *self, PyObject *args) {
    int sampleRate, sampleSize, channels;
    if (!PyArg_ParseTuple(args, "iii", &sampleRate, &sampleSize, &channels)) {
        return NULL; // Error parsing arguments
    }
    if (sampleRate <= 0 || sampleSize <= 0 || channels <= 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid format parameters");
        return NULL;
    }
    WaveFormat(&self->wave, sampleRate, sampleSize, channels);
    Py_RETURN_NONE;
}

static PyMethodDef PalWave_methods[] = {
    {"is_ready", (PyCFunction)pywave_is_ready, METH_VARARGS, "Is Wave ready?"},
    {"export", (PyCFunction)pywave_export, METH_VARARGS, "Export Wave to file"},
    {"copy", (PyCFunction)pywave_copy, METH_VARARGS, "Clone a Wave object"},
    {"crop", (PyCFunction)pywave_crop, METH_VARARGS, "Crop a Wave to defined samples range"},
    {"format", (PyCFunction)pywave_format, METH_VARARGS, "Convert Wave data to desired format"},
    {"from_buffer", (PyCFunction)pywave_setpointer, METH_O, "Set memory pointer from a buffer"},
    {"samples", (PyCFunction)pywave_samples, METH_VARARGS, "Load samples data from wave as a floats array"},
    {NULL, NULL, 0, NULL}
};

static PyObject* PalWave_get_frame_count(PalWave *self, void *closure) {
    return PyLong_FromLong(self->wave.frameCount);
}

static PyObject* PalWave_get_sample_rate(PalWave *self, void *closure) {
    return PyLong_FromLong(self->wave.sampleRate);
}

static PyObject* PalWave_get_sample_size(PalWave *self, void *closure) {
    return PyLong_FromLong(self->wave.sampleSize);
}

static PyObject* PalWave_get_channels(PalWave *self, void *closure) {
    return PyLong_FromLong(self->wave.channels);
}

static PyGetSetDef PalWave_attrs[] = {
    {"frame_count", (getter)PalWave_get_frame_count, NULL, "Total number of frames (considering channels)", NULL},
    {"sample_rate", (getter)PalWave_get_sample_rate, NULL, "Frequency (samples per second)", NULL},
    {"sample_size", (getter)PalWave_get_sample_size, NULL, "Bit depth (bits per sample): 8, 16, 32 (24 not supported)", NULL},
    {"channels", (getter)PalWave_get_channels, NULL, "Number of channels (1-mono, 2-stereo, ...)", NULL},
    {NULL}  // Sentinel
};

static PyTypeObject PalWaveType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "pal.Wave",                                 /* tp_name */
    sizeof(PalWave),                            /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)PalWave_Dealloc,                /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    &PalWave_mapping,                           /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    &PalWave_as_buffer,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                         /* tp_flags */
    PyDoc_STR("PalWave object"),                /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    PalWave_methods,                            /* tp_methods */
    0,                                          /* tp_members */
    PalWave_attrs,                              /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    (initproc)PalWave_Init,                     /* tp_init */
    0,                                          /* tp_alloc */
    (newfunc)PalWave_New,                       /* tp_new */
};

// typedef struct AudioStream {
//    rAudioBuffer *buffer;       // Pointer to internal data used by the audio system
//    rAudioProcessor *processor; // Pointer to internal data processor, useful for audio effects

//    unsigned int sampleRate;    // Frequency (samples per second)
//    unsigned int sampleSize;    // Bit depth (bits per sample): 8, 16, 32 (24 not supported)
//    unsigned int channels;      // Number of channels (1-mono, 2-stereo, ...)
// } AudioStream;

typedef struct {
    PyObject_HEAD
    AudioStream stream;
} PalAudioStream;

static PyTypeObject PalAudioStreamType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "pal.AudioStream",                                 /* tp_name */
    sizeof(PalAudioStream),                            /* tp_basicsize */
};

// typedef struct Sound {
//    AudioStream stream;         // Audio stream
//    unsigned int frameCount;    // Total number of frames (considering channels)
// } Sound;

typedef struct {
    PyObject_HEAD
    Sound sound;
} PalSound;

static int PalSound_Init(PalSound *self, PyObject *args, PyObject *kwds) {
    PyObject *source = NULL;  // Could be a string or a Wave object
    static char *kwlist[] = {"source", "path", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kwlist, &source, NULL)) // "O|O" for optional object
        return -1;
    if (!source) {
        PyErr_SetString(PyExc_TypeError, "Must provide a path or Wave object");
        return -1;
    }
    
    if (PyUnicode_Check(source)) {
        const char *path = PyUnicode_AsUTF8(source);
        if (path == NULL)
            return -1;
        self->sound = LoadSound(path);
        if (!IsSoundReady(self->sound))
            return -1;
    } else if (PyObject_TypeCheck(source, &PalWaveType)) {
        PalWave *wave_obj = (PalWave*)source;
        self->sound = LoadSoundFromWave(wave_obj->wave);
        if (!IsSoundReady(self->sound))
            return -1;
    } else {
        PyErr_SetString(PyExc_TypeError, "Source must be a path (string) or a Wave object");
        return -1;
    }
    return 0;
}

#define SET_ATTR(NAME, DEF_VAL) \
do { \
    PyObject *obj = PyFloat_FromDouble(DEF_VAL); \
    if (!obj) { \
        Py_XDECREF(obj); \
        Py_DECREF(self); \
        return PyErr_NoMemory(); \
    } \
    if (PyObject_SetAttrString((PyObject*)self, #NAME, obj) < 0) { \
        Py_DECREF(obj); \
        Py_DECREF(self); \
        return NULL; \
    } \
    Py_DECREF(obj); \
} while (0)

static PyObject* PalSound_New(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PalSound* self = (PalSound*)type->tp_alloc(type, 0);
    if (!self)
        return PyErr_NoMemory();
    memset(&self->sound, 0, sizeof(Sound));
    SET_ATTR(volume, 1.0);
    SET_ATTR(pitch, 1.0);
    SET_ATTR(pan, 1.0);
    return (PyObject*)self;
}

static void PalSound_Dealloc(PalSound* self) {
    if (IsSoundReady(self->sound))
        UnloadSound(self->sound);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static Py_ssize_t PalSound_length(PalSound *self) {
    float seconds = (float)self->sound.frameCount*1000.0/(self->sound.stream.sampleRate*self->sound.stream.channels);
    return (Py_ssize_t)seconds;
}

static PyMappingMethods PalSound_mapping = {
    (lenfunc)PalSound_length, NULL, NULL
};

static PyObject* pysound_is_ready(PalSound *self, PyObject *args) {
    if (IsSoundReady(self->sound)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* pysound_update(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL; // Error parsing arguments
    }
    // TODO
    Py_RETURN_NONE;
}

static PyObject* pysound_play(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL; // Error parsing arguments
    }
    PlaySound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* pysound_stop(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL; // Error parsing arguments
    }
    StopSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* pysound_pause(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL; // Error parsing arguments
    }
    PauseSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* pysound_resume(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL; // Error parsing arguments
    }
    ResumeSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* pysound_is_playing(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL; // Error parsing arguments
    }
    if (IsSoundPlaying(self->sound)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyMethodDef PalSound_methods[] = {
    {"is_ready", (PyCFunction)pysound_is_ready, METH_VARARGS, "Is Wave ready?"},
    {"update", (PyCFunction)pysound_update, METH_VARARGS, "Update sound buffer with new data"},
    {"play", (PyCFunction)pysound_play, METH_VARARGS, "Play a sound" },
    {"stop", (PyCFunction)pysound_stop, METH_VARARGS, "Stop playing a sound" },
    {"pause", (PyCFunction)pysound_pause, METH_VARARGS, "Pause a sound" },
    {"resume", (PyCFunction)pysound_resume, METH_VARARGS, "Resume a paused sound" },
    {"is_playing", (PyCFunction)pysound_is_playing, METH_VARARGS, "Check if a sound is currently playing" },
    {NULL, NULL, 0, NULL}
};

static PyObject* PalSound_get_frame_count(PalSound *self, void *closure) {
    return PyLong_FromLong(self->sound.frameCount);
}

static PyObject* pysound_get_volume(PalSound *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "volume");
}

static int pysound_set_volume(PalSound *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'volume' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "volume", value);
    SetSoundVolume(self->sound, (float)vol);
    return 0;
}

static PyObject* pysound_get_pitch(PalSound *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "pitch");
}

static int pysound_set_pitch(PalSound *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pitch' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "pitch", value);
    SetSoundVolume(self->sound, (float)vol);
    return 0;
}

static PyObject* pysound_get_pan(PalSound *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "pan");
}

static int pysound_set_pan(PalSound *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pan' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "pan", value);
    SetSoundVolume(self->sound, (float)vol);
    return 0;
}

static PyGetSetDef PalSound_attrs[] = {
    {"frame_count", (getter)PalSound_get_frame_count, NULL, "Total number of frames (considering channels)", NULL},
    {"volume", (getter)pysound_get_volume, (setter)pysound_set_volume, "Sound volume", NULL},
    {"pitch", (getter)pysound_get_pitch, (setter)pysound_set_pitch, "Sound pitch", NULL},
    {"pan", (getter)pysound_get_pan, (setter)pysound_set_pan, "Sound pan", NULL},
    {NULL}  // Sentinel
};

static PyTypeObject PalSoundType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "pal.Sound",                                /* tp_name */
    sizeof(PalSound),                           /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)PalSound_Dealloc,               /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    &PalSound_mapping,                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                         /* tp_flags */
    PyDoc_STR("PalSound object"),               /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    PalSound_methods,                           /* tp_methods */
    0,                                          /* tp_members */
    PalSound_attrs,                             /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    (initproc)PalSound_Init,                    /* tp_init */
    0,                                          /* tp_alloc */
    (newfunc)PalSound_New,                      /* tp_new */
};

// Music, audio stream, anything longer than ~10 seconds should be streamed
// typedef struct Music {
//    AudioStream stream;         // Audio stream
//    unsigned int frameCount;    // Total number of frames (considering channels)
//    bool looping;               // Music looping enable

//    int ctxType;                // Type of music context (audio filetype)
//    void *ctxData;              // Audio context data, depends on type
// } Music;

typedef struct {
    PyObject_HEAD
    Music music;
} PalMusic;

static PyTypeObject PalMusicType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "pal.Music",                                 /* tp_name */
    sizeof(PalMusic),                            /* tp_basicsize */
};

static PyObject* pal_initialize(PyObject *self, PyObject *args) {
    if (IsAudioDeviceReady())
        Py_RETURN_NONE;
    InitAudioDevice();
    if (!IsAudioDeviceReady())
        PyErr_SetString(PyExc_SystemError, "Failed to initialize audio device");
    Py_RETURN_NONE;
}

static PyObject* pal_shutdown(PyObject *self, PyObject *args) {
    if (IsAudioDeviceReady())
        CloseAudioDevice();
    Py_RETURN_NONE;
}

static PyObject* pal_is_ready(PyObject *self, PyObject *args) {
    if (IsAudioDeviceReady()) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* pal_set_master_volume(PyObject *self, PyObject *args) {
    float volume;
    if (!PyArg_ParseTuple(args, "f", &volume))
        return NULL;
    if (volume < 0.0f || volume > 1.0f) {
        PyErr_SetString(PyExc_ValueError, "Volume must be between 0.0 and 1.0");
        return NULL;
    }
    SetMasterVolume(volume);
    Py_RETURN_NONE;
}

static PyObject* pal_get_master_volume(PyObject *self, PyObject *args) {
    return PyFloat_FromDouble((double)GetMasterVolume());
}

static PyMethodDef pal_methods[] = {
    {"initialize", pal_initialize, METH_VARARGS, "Initialize audio device"},
    {"shutdown", pal_shutdown, METH_VARARGS, "Shutdown audio device"},
    {"is_ready", pal_is_ready, METH_VARARGS, "Is audio device ready?"},
    {"set_master_volume", pal_set_master_volume, METH_VARARGS, "Set master volume"},
    {"get_master_volume", pal_get_master_volume, METH_VARARGS, "Get master volume"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef pal_module = {
    PyModuleDef_HEAD_INIT,
    "pal",
    "Python Audio Libray (bindings for raudio)",
    -1,
    pal_methods,
};

#define STRUCTS \
    X(Wave) \
    X(AudioStream) \
    X(Sound) \
    X(Music)

PyMODINIT_FUNC PyInit_pal(void) {
#define X(NAME) \
    if (PyType_Ready(&Pal##NAME##Type) < 0) \
        return NULL;
    STRUCTS
#undef X
    PyObject *m = PyModule_Create(&pal_module);
    if (!m)
        return NULL;
#define X(NAME) \
    Py_INCREF(&Pal##NAME##Type); \
    if (PyModule_AddObject(m, "pal", (PyObject*)&Pal##NAME##Type) < 0) { \
    Py_DECREF(&Pal##NAME##Type); \
        Py_DECREF(m); \
        return NULL; \
    }
    STRUCTS
#undef X
    return m;
}
