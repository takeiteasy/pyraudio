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
        return -1;
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

static PyObject* PalWave_is_ready(PalWave *self, PyObject *args) {
    if (IsWaveReady(self->wave)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* PalWave_export(PalWave *self, PyObject *args, PyObject *kwds) {
    char *path = NULL;  // Initialize to NULL
    static char *kwlist[] = {"path", NULL}; // Keyword arguments
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path)) {
        PyErr_SetString(PyExc_RuntimeError, "No path passed to export");
        return NULL;
    }
    if (!ExportWave(self->wave, path)) {
        PyErr_Format(PyExc_OSError, "Failed to write to file \"%s\"", path);
        return NULL;
    }
    Py_RETURN_TRUE;
}

static PyObject* PalWave_copy(PalWave *self, PyObject *args, PyObject *kwds) {
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

static PyObject* PalWave_update(PalWave *self, PyObject *original) {
    Py_buffer buffer;
    if (PyObject_GetBuffer(original, &buffer, PyBUF_WRITABLE) == -1)
        return NULL;
    self->wave.data = buffer.buf;
    self->wave.frameCount = buffer.len / (self->wave.channels * sizeof(short));
    PyBuffer_Release(&buffer);
    return Py_BuildValue("d", 1);
}

static PyObject* PalWave_samples(PalWave *self, PyObject *args) {
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

static PyObject* PalWave_crop(PalWave *self, PyObject *args) {
    unsigned int initSample, finalSample;
    if (!PyArg_ParseTuple(args, "ii", &initSample, &finalSample)) {
        return NULL;
    }
    if (initSample < 0 || finalSample < 0 || initSample >= finalSample ||
        initSample >= self->wave.frameCount || finalSample > self->wave.frameCount) {
        PyErr_SetString(PyExc_ValueError, "Invalid sample range for crop");
        return NULL;
    }
    WaveCrop(&self->wave, initSample, finalSample);
    Py_RETURN_NONE;
}

static PyObject* PalWave_format(PalWave *self, PyObject *args) {
    int sampleRate, sampleSize, channels;
    if (!PyArg_ParseTuple(args, "iii", &sampleRate, &sampleSize, &channels)) {
        return NULL;
    }
    if (sampleRate <= 0 || sampleSize <= 0 || channels <= 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid format parameters");
        return NULL;
    }
    WaveFormat(&self->wave, sampleRate, sampleSize, channels);
    Py_RETURN_NONE;
}

static PyMethodDef PalWave_methods[] = {
    {"is_ready", (PyCFunction)PalWave_is_ready, METH_VARARGS, "Is Wave ready?"},
    {"export", (PyCFunction)PalWave_export, METH_VARARGS, "Export Wave to file"},
    {"copy", (PyCFunction)PalWave_copy, METH_VARARGS, "Clone a Wave object"},
    {"crop", (PyCFunction)PalWave_crop, METH_VARARGS, "Crop a Wave to defined samples range"},
    {"format", (PyCFunction)PalWave_format, METH_VARARGS, "Convert Wave data to desired format"},
    {"update", (PyCFunction)PalWave_update, METH_O, "Set memory pointer from a buffer"},
    {"samples", (PyCFunction)PalWave_samples, METH_VARARGS, "Load samples data from wave as a floats array"},
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
    {NULL}
};

static PyTypeObject PalWaveType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "pal.Wave",
    .tp_basicsize = sizeof(PalWave),
    .tp_dealloc = (destructor)PalWave_Dealloc,
    .tp_as_mapping = &PalWave_mapping,
    .tp_as_buffer = &PalWave_as_buffer,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("PalWave object"),
    .tp_methods = PalWave_methods,
    .tp_getset = PalWave_attrs,
    .tp_init = (initproc)PalWave_Init,
    .tp_new = (newfunc)PalWave_New,
};

typedef struct {
    PyObject_HEAD
    AudioStream stream;
} PalAudioStream;

static int PalAudioStream_Init(PalAudioStream *self, PyObject *args, PyObject *kwds) {
    unsigned int sampleRate, sampleSize, channels;
    if (!PyArg_ParseTuple(args, "iii", &sampleRate, &sampleSize, &channels)) {
        PyErr_SetString(PyExc_ValueError, "Invalid parameters");
        return -1;
    }
    self->stream = LoadAudioStream(sampleRate, sampleSize, channels);
    if (!IsAudioStreamReady(self->stream))
        return -1;
    return 0;
}

static PyObject* PalAudioStream_New(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PalAudioStream* self = (PalAudioStream*)type->tp_alloc(type, 0);
    if (!self)
        return PyErr_NoMemory();
    memset(&self->stream, 0, sizeof(AudioStream));
    return (PyObject*)self;
}

static void PalAudioStream_Dealloc(PalAudioStream* self) {
    if (IsAudioStreamReady(self->stream))
        UnloadAudioStream(self->stream);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* PalAudioStream_is_ready(PalAudioStream *self, PyObject *args) {
    if (IsAudioStreamReady(self->stream)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* PalAudioStream_play(PalAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    PlayAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* PalAudioStream_stop(PalAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    StopAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* PalAudioStream_pause(PalAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    PauseAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* PalAudioStream_resume(PalAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    ResumeAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* PalAudioStream_is_playing(PalAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    if (IsAudioStreamPlaying(self->stream)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* PalAudioStream_update(PalAudioStream *self, PyObject *buf) {
    Py_buffer buffer;
    if (PyObject_GetBuffer(buf, &buffer, PyBUF_WRITABLE) == -1)
        return NULL;
    UpdateAudioStream(self->stream, buffer.buf, buffer.len / (self->stream.channels * sizeof(short)));
    PyBuffer_Release(&buffer);
    return Py_BuildValue("d", 1);
}

static PyMethodDef PalAudioStream_methods[] = {
    {"is_ready", (PyCFunction)PalAudioStream_is_ready, METH_VARARGS, "Is AudioStream ready?"},
    {"play", (PyCFunction)PalAudioStream_play, METH_VARARGS, "Play a AudioStream" },
    {"stop", (PyCFunction)PalAudioStream_stop, METH_VARARGS, "Stop playing a AudioStream" },
    {"pause", (PyCFunction)PalAudioStream_pause, METH_VARARGS, "Pause a AudioStream" },
    {"resume", (PyCFunction)PalAudioStream_resume, METH_VARARGS, "Resume a paused AudioStream" },
    {"is_playing", (PyCFunction)PalAudioStream_is_playing, METH_VARARGS, "Check if a AudioStream is currently playing" },
    {"update", (PyCFunction)PalAudioStream_update, METH_VARARGS, "Update AudioStream with a new buffer"},
    {NULL}
};

static PyObject* PalAudioStream_get_sample_rate(PalAudioStream *self, void *closure) {
    return PyLong_FromUnsignedLong(self->stream.sampleRate);
}

static PyObject* PalAudioStream_get_sample_size(PalAudioStream *self, void *closure) {
    return PyLong_FromUnsignedLong(self->stream.sampleSize);
}

static PyObject* PalAudioStream_get_channels(PalAudioStream *self, void *closure) {
    return PyLong_FromUnsignedLong(self->stream.channels);
}

static PyGetSetDef PalAudioStream_attrs[] = {
    {"sample_rate", (getter)PalAudioStream_get_sample_rate, NULL, "AudioStream sample rate", NULL},
    {"sample_size", (getter)PalAudioStream_get_sample_size, NULL, "AudioStream sample size", NULL},
    {"channels", (getter)PalAudioStream_get_channels, NULL, "AudioStream channels", NULL},
    {NULL}
};

static PyTypeObject PalAudioStreamType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "pal.AudioStream",
    .tp_basicsize = sizeof(PalAudioStream),
    .tp_dealloc = (destructor)PalAudioStream_Dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("PalAudioStream object"),
    .tp_methods = PalAudioStream_methods,
    .tp_getset = PalAudioStream_attrs,
    .tp_init = (initproc)PalAudioStream_Init,
    .tp_new = (newfunc)PalAudioStream_New,
};

typedef struct {
    PyObject_HEAD
    Sound sound;
} PalSound;

static int PalSound_Init(PalSound *self, PyObject *args, PyObject *kwds) {
    PyObject *source = NULL;  // Could be a string or a Wave object
    static char *kwlist[] = {"path", NULL};
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
    SET_ATTR(_volume, 1.0);
    SET_ATTR(_pitch, 1.0);
    SET_ATTR(_pan, 1.0);
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

static PyObject* PalSound_is_ready(PalSound *self, PyObject *args) {
    if (IsSoundReady(self->sound)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* PalSound_play(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    PlaySound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* PalSound_stop(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    StopSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* PalSound_pause(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    PauseSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* PalSound_resume(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    ResumeSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* PalSound_is_playing(PalSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    if (IsSoundPlaying(self->sound)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* PalSound_update(PalSound *self, PyObject *buf) {
    Py_buffer buffer;
    if (PyObject_GetBuffer(buf, &buffer, PyBUF_WRITABLE) == -1)
        return NULL;
    UpdateSound(self->sound, buffer.buf, buffer.len / (self->sound.stream.channels * sizeof(short)));
    PyBuffer_Release(&buffer);
    return Py_BuildValue("d", 1);
}

static PyMethodDef PalSound_methods[] = {
    {"is_ready", (PyCFunction)PalSound_is_ready, METH_VARARGS, "Is Sound ready?"},
    {"play", (PyCFunction)PalSound_play, METH_VARARGS, "Play a Sound" },
    {"stop", (PyCFunction)PalSound_stop, METH_VARARGS, "Stop playing a Sound" },
    {"pause", (PyCFunction)PalSound_pause, METH_VARARGS, "Pause a Sound" },
    {"resume", (PyCFunction)PalSound_resume, METH_VARARGS, "Resume a paused Sound" },
    {"is_playing", (PyCFunction)PalSound_is_playing, METH_VARARGS, "Check if a Sound is currently playing" },
    {"update", (PyCFunction)PalSound_update, METH_VARARGS, "Update a Sound with a new buffer" },
    {NULL, NULL, 0, NULL}
};

static PyObject* PalSound_get_frame_count(PalSound *self, void *closure) {
    return PyLong_FromLong(self->sound.frameCount);
}

static PyObject* PalSound_get_volume(PalSound *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "_volume");
}

static int PalSound_set_volume(PalSound *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'volume' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "_volume", value);
    SetSoundVolume(self->sound, (float)vol);
    return 0;
}

static PyObject* PalSound_get_pitch(PalSound *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "_pitch");
}

static int PalSound_set_pitch(PalSound *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pitch' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "_pitch", value);
    SetSoundVolume(self->sound, (float)vol);
    return 0;
}

static PyObject* PalSound_get_pan(PalSound *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "_pan");
}

static int PalSound_set_pan(PalSound *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pan' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "_pan", value);
    SetSoundVolume(self->sound, (float)vol);
    return 0;
}

static PyObject* PalSound_get_stream(PalSound *self, void *closure) {
    PalAudioStream *stream = (PyObject*)PalAudioStream.tp_alloc(&PalAudioStreamType, 0);
    if (!stream)
        return PyErr_NoMemory();
    memcpy(&self->sound.stream, &stream->stream, sizeof(AudioStream));
    return (PyObject*)stream;
}

static int PalSound_set_stream(PalSound *self, PyObject *value, void *closure) {
    PalAudioStream *stream = (PalAudioStream*)source;
    if (!PyObject_TypeCheck(value, &PalAudioStreamType))
        return -1;
    memcpy(&stream->stream, &self->sound.stream, sizeof(AudioStream));
    Py_DECREF(stream);
    return 0;
}

static PyGetSetDef PalSound_attrs[] = {
    {"frame_count", (getter)PalSound_get_frame_count, NULL, "Total number of frames (considering channels)", NULL},
    {"volume", (getter)PalSound_get_volume, (setter)PalSound_set_volume, "Sound volume", NULL},
    {"pitch", (getter)PalSound_get_pitch, (setter)PalSound_set_pitch, "Sound pitch", NULL},
    {"pan", (getter)PalSound_get_pan, (setter)PalSound_set_pan, "Sound pan", NULL},
    {"stream", (getter)PalSound_get_stream, (setter)PalSound_set_stream, "AudioStream object", NULL},
    {NULL}
};

static PyTypeObject PalSoundType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "pal.Sound",
    .tp_basicsize = sizeof(PalSound),
    .tp_dealloc = (destructor)PalSound_Dealloc,
    .tp_as_mapping = &PalSound_mapping,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("PalSound object"),
    .tp_methods = PalSound_methods,
    .tp_getset = PalSound_attrs,
    .tp_init = (initproc)PalSound_Init,
    .tp_new = (newfunc)PalSound_New,
};

// Music, audio stream, anything longer than ~10 seconds should be streamed
typedef struct {
    PyObject_HEAD
    Music music;
} PalMusic;

static int PalMusic_Init(PalMusic *self, PyObject *args, PyObject *kwds) {
    char *path = NULL;  // Initialize to NULL
    static char *kwlist[] = {"path", NULL}; // Keyword arguments
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path))
        return -1;
    if (path) {
        PyObject *path_obj = PyUnicode_FromString(path);
        if (PyObject_SetAttrString((PyObject*)self, "path", path_obj) < 0) {
            Py_DECREF(path_obj);
            return -1;
        }
        Py_DECREF(path_obj);
        self->music = LoadMusicStream(path);
        if (!IsMusicReady(self->music))
            return -1;
    }
    return 0;
}

static PyObject* PalMusic_New(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PalMusic* self = (PalMusic*)type->tp_alloc(type, 0);
    if (!self)
        return PyErr_NoMemory();
    memset(&self->music, 0, sizeof(Music));
    SET_ATTR(volume, 1.0);
    SET_ATTR(pitch, 1.0);
    SET_ATTR(pan, 1.0);
    return (PyObject*)self;
}

static void PalMusic_Dealloc(PalMusic* self) {
    if (IsMusicReady(self->music))
        UnloadMusicStream(self->music);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static Py_ssize_t PalMusic_length(PalMusic *self) {
    return (Py_ssize_t)GetMusicTimeLength(self->music);
}

static PyMappingMethods PalMusic_mapping = {
    (lenfunc)PalMusic_length, NULL, NULL
};

static PyObject* PalMusic_is_ready(PalMusic *self, PyObject *args) {
    if (IsMusicReady(self->music)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* PalMusic_play(PalMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    PlayMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* PalMusic_stop(PalMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    StopMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* PalMusic_pause(PalMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    PauseMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* PalMusic_resume(PalMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    ResumeMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* PalMusic_is_playing(PalMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    if (IsMusicStreamPlaying(self->music)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* PalMusic_seek(PalMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    float offset;
    if (!PyArg_ParseTuple(args, "f", &offset)) {
        PyErr_SetString(PyExc_TypeError, "Seek offset must be a float");
        return NULL;
    }
    if (offset < 0.0)
        offset = 0.0;
    SeekMusicStream(self->music, offset);
    Py_RETURN_NONE;
}

static PyObject* PalMusic_update(PalMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    UpdateMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyMethodDef PalMusic_methods[] = {
    {"is_ready", (PyCFunction)PalMusic_is_ready, METH_VARARGS, "Is Music ready?"},
    {"play", (PyCFunction)PalMusic_play, METH_VARARGS, "Play a Music" },
    {"stop", (PyCFunction)PalMusic_stop, METH_VARARGS, "Stop playing a Music" },
    {"pause", (PyCFunction)PalMusic_pause, METH_VARARGS, "Pause a Music" },
    {"resume", (PyCFunction)PalMusic_resume, METH_VARARGS, "Resume a paused Music" },
    {"seek", (PyCFunction)PalMusic_seek, METH_VARARGS, "Seek Music to a position (in seconds)" },
    {"is_playing", (PyCFunction)PalMusic_is_playing, METH_VARARGS, "Check if a Music is currently playing" },
    {"update", (PyCFunction)PalMusic_update, METH_VARARGS, "Update (re-fill) Music buffers if data already processed"},
    {NULL, NULL, 0, NULL}
};

static PyObject* PalMusic_get_frame_count(PalMusic *self, void *closure) {
    return PyLong_FromLong(self->music.frameCount);
}

static PyObject* PalMusic_get_volume(PalMusic *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "_volume");
}

static int PalMusic_set_volume(PalMusic *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'volume' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "_volume", value);
    SetMusicVolume(self->music, (float)vol);
    return 0;
}

static PyObject* PalMusic_get_pitch(PalMusic *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "_pitch");
}

static int PalMusic_set_pitch(PalMusic *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pitch' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "_pitch", value);
    SetMusicVolume(self->music, (float)vol);
    return 0;
}

static PyObject* PalMusic_get_pan(PalMusic *self, void *closure) {
    return PyObject_GetAttrString((PyObject*)self, "_pan");
}

static int PalMusic_set_pan(PalMusic *self, PyObject *value, void *closure) {
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pan' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred() != NULL)
        return -1;
    PyObject_SetAttrString((PyObject*)self, "_pan", value);
    SetMusicVolume(self->music, (float)vol);
    return 0;
}

static PyObject* PalMusic_get_position(PalMusic *self, void *closure) {
    PyObject *obj = PyFloat_FromDouble(IsMusicReady(self->music) ? GetMusicTimePlayed(self->music) : 0.0);
    if (!obj)
        return PyErr_NoMemory();
    return obj;
}

static int PalMusic_set_position(PalMusic *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'position' attribute");
        return -1;
    }
    if (PyErr_Occurred() != NULL)
        return -1;
    SeekMusicStream(self->music, (float)PyFloat_AsDouble(value));
    return 0;
}

static PyObject* PalMusic_get_looping(PalMusic *self, void *closure) {
    if (self->music.looping) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static int PalMusic_set_looping(PalMusic *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'loop' attribute");
        return -1;
    }
    if (PyBool_Check(value))
        self->music.looping = value == Py_True;
    else {
        PyErr_SetString(PyExc_TypeError, "Loop attribute must be a boolean");
        return -1;
    }
    return 0;
}

static PyObject* PalMusic_get_stream(PalMusic *self, void *closure) {
    PalAudioStream *stream = (PyObject*)PalAudioStream.tp_alloc(&PalAudioStreamType, 0);
    if (!stream)
        return PyErr_NoMemory();
    memcpy(&self->music.stream, &stream->stream, sizeof(AudioStream));
    return (PyObject*)stream;
}

static int PalMusic_set_stream(PalMusic *self, PyObject *value, void *closure) {
    PalAudioStream *stream = (PalAudioStream*)source;
    if (!PyObject_TypeCheck(value, &PalAudioStreamType))
        return -1;
    memcpy(&stream->stream, &self->music.stream, sizeof(AudioStream));
    Py_DECREF(stream);
    return 0;
}

static PyGetSetDef PalMusic_attrs[] = {
    {"frame_count", (getter)PalMusic_get_frame_count, NULL, "Total number of frames (considering channels)", NULL},
    {"volume", (getter)PalMusic_get_volume, (setter)PalMusic_set_volume, "Music volume", NULL},
    {"pitch", (getter)PalMusic_get_pitch, (setter)PalMusic_set_pitch, "Music pitch", NULL},
    {"pan", (getter)PalMusic_get_pan, (setter)PalMusic_set_pan, "Music pan", NULL},
    {"position", (getter)PalMusic_get_position, (setter)PalMusic_set_position, "Music position", NULL},
    {"loop", (getter)PalMusic_get_looping, (setter)PalMusic_set_looping, "Music looping", NULL},
    {"stream", (getter)PalMusic_get_stream, (setter)PalMusic_set_stream, "AudioStream object", NULL},
    {NULL}
};

static PyTypeObject PalMusicType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "pal.Music",
    .tp_basicsize = sizeof(PalMusic),
    .tp_dealloc = (destructor)PalMusic_Dealloc,
    .tp_as_mapping = &PalMusic_mapping,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("PalMusic object"),
    .tp_methods = PalMusic_methods,
    .tp_getset = PalMusic_attrs,
    .tp_init = (initproc)PalMusic_Init,
    .tp_new = (newfunc)PalMusic_New,
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
