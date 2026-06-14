#include <pybind11/pybind11.h>
#include "global.h"


namespace pybind11 { namespace detail {
template <> struct type_caster<QString> {
public:
    PYBIND11_TYPE_CASTER(QString, _("str"));

    // 从 Python str 转换到 QString
    bool load(handle src, bool convert) {
        PyObject* source = src.ptr();
        if (!PyUnicode_Check(source)) {
            if (convert) {
                PyObject* temp = PyUnicode_FromObject(source);
                if (!temp) return false;
                // 使用 UTF-8 转换，更简单且无内存泄漏
                const char* utf8 = PyUnicode_AsUTF8(temp);
                if (!utf8) {
                    Py_DECREF(temp);
                    return false;
                }
                value = QString::fromUtf8(utf8);
                Py_DECREF(temp);
                return true;
            }
            return false;
        }
        const char* utf8 = PyUnicode_AsUTF8(source);
        if (!utf8) return false;
        value = QString::fromUtf8(utf8);
        return true;
    }

    static handle cast(QString src, return_value_policy policy, handle parent) {
        QByteArray utf8 = src.toUtf8();
        PyObject* result = PyUnicode_FromStringAndSize(utf8.constData(), utf8.size());
        if (!result) throw error_already_set();
        return result;
    }
};
}} // namespace pybind11::detail

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(qq_api, m, py::mod_gil_not_used()) {
    m.doc() = "QQ API binding with uid support";
    py::class_<MessageEvent>(m, "MessageEvent")
    .def(py::init<>())
        .def_readwrite("groupid", &MessageEvent::groupId)
        .def_readwrite("user", &MessageEvent::user)
        .def_readwrite("msgid", &MessageEvent::msgId)
        .def_readwrite("msg", &MessageEvent::msg)
        .def_readwrite("seq", &MessageEvent::seq)
        .def_readwrite("appid", &MessageEvent::appid)
        .def_readwrite("user_int", &MessageEvent::user_int)
        .def_readwrite("type", &MessageEvent::type)
        .def_readwrite("subtype", &MessageEvent::subType)
        .def_readwrite("callbacktype", &MessageEvent::callbackType)
        .def_readwrite("fulltype", &MessageEvent::fullType)
        .def_readwrite("nickname", &MessageEvent::nickname)
        .def_readwrite("guildId", &MessageEvent::guildId)
        .def_readwrite("msgtype", &MessageEvent::msgType)
        .def_readwrite("at_you", &MessageEvent::at_you)
        .def_readwrite("raw", &MessageEvent::raw)
        .def_readwrite("callbackid", &MessageEvent::callbackId)
        .def_readwrite("replyto", &MessageEvent::replyTo);


    m.def("Callback", &myCallbackA, "A callback function",
          py::arg("uuid"), py::arg("apiId"), py::arg("appid"),
          py::arg("_1"), py::arg("_2"), py::arg("_3"), py::arg("_4"),
          py::arg("_5"), py::arg("_6"), py::arg("_7"), py::arg("_8"));
}