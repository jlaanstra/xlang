#pragma once

namespace xlang
{
    void write_import_type(writer& w, TypeDef const& type)
    {
        if (is_exclusive_to(type))
        {
            return;
        }

        w.write("@ = __ns__.@\n", type.TypeName(), type.TypeName());
    }

	void write_snake_case(writer& w, std::string_view const& name, bool uppercase)
	{
		auto case_func = [uppercase](char c)
		{
			return uppercase
				? static_cast<char>(::toupper(c))
				: static_cast<char>(::tolower(c));
		};

		w.write(case_func(name[0]));
		for (int i = 1; i < name.size() - 1; i++)
		{
			if (isupper(name[i]) && islower(name[i + 1]))
			{
				w.write('_');
			}

			w.write(case_func(name[i]));
		}
		w.write(case_func(name[name.size() - 1]));
	}

    void write_python_enum(writer& w, TypeDef const& type)
    {

        w.write("class %(enum.%):\n", type.TypeName(), is_flags_enum(type) ? "IntFlag" : "IntEnum");
        {
            writer::indent_guard g{ w };

            for(auto&& field : type.FieldList())
            {
                if (auto constant = field.Constant())
                {
                    w.write("% = %\n", bind<write_snake_case>(field.Name(), true), *constant);
                }
            }
        }

        w.write("\n");
    }

    void write_method_body(writer& w, TypeDef const& type, method_info const& info);
    void write_method_overloads(writer& w, TypeDef const& type, std::vector<xlang::method_info> const& overloads);
    void write_winrt_type_specialization_storage(writer& w, TypeDef const& type);
    void write_method_table(writer& w, TypeDef const& type);
    void write_getset_table(writer& w, TypeDef const& type);
    void write_type_slot_table(writer& w, TypeDef const& type);
    void write_type_spec(writer& w, TypeDef const& type);

    void write_include(writer& w, std::string_view const& ns)
    {
        if (w.current_namespace != ns)
        {
            auto format = R"(
#if __has_include("py.%.h")
#include "py.%.h"
#endif
)";
            w.write(format, ns, ns);
        }
        else
        {
            w.write("#include \"py.%.h\"\n", ns);
        }
    }

    void write_try_catch(writer& w, std::function<void(writer&)> tryfunc, std::function<void(writer&)> catchfunc)
    {
        w.write("try\n{\n");
        {
            writer::indent_guard g{ w };
            tryfunc(w);
        }
        w.write("}\ncatch (...)\n{\n");
        {
            writer::indent_guard g{ w };
            catchfunc(w);
        }
        w.write("}\n");
    }

    void write_try_catch(writer& w, std::function<void(writer&)> func, std::string_view const& catch_return = "py::to_PyErr()")
    {
        write_try_catch(w, func, [&catch_return](writer& w) { w.write("return %;\n", catch_return); });
    }


    void write_detach_param(writer& w, std::string const& paramName)
    {
        w.write("%.detach()", paramName);
    }
    
    void write_param_name(writer& w, method_signature::param_t param)
    {
        w.register_type_namespace(param.second->Type());
        w.write("param%", param.first.Sequence() - 1);
    }

    void write_type_name(writer& w, TypeDef const& type)
    {
        if (is_ptype(type))
        {
            w.write("py@", type.TypeName());
        }
        else
        {
            w.write("%", type);
        }
    }

    void write_wrapper_type(writer& w, TypeDef const& type)
    {
        auto wrapper_type = is_ptype(type) ? "winrt_pinterface_wrapper" :
            get_category(type) == category::struct_type ? "winrt_struct_wrapper" : "winrt_wrapper";

        w.write("py::%<%>", wrapper_type, bind<write_type_name>(type));
    }

    void write_pinterface_type_arg_name(writer& w, GenericParam const& param)
    {
        w.write(param.Name());
    }

    void write_pinterface_type_arg(writer& w, GenericParam const& param)
    {
        w.write("typename %", bind<write_pinterface_type_arg_name>(param));
    }

    void write_full_type_name(writer& w, TypeDef const& type)
    {
        w.write(type);

        if (is_ptype(type))
        {
            w.write("<%>", bind_list<write_pinterface_type_arg_name>(", ", type.GenericParam()));
        }
    }

    void write_get_python_type_specialization(writer& w, TypeDef const& type)
    {
        if (is_exclusive_to(type))
        {
            return;
        }

        auto format = R"(template<>
struct winrt_type<%>
{
    static PyTypeObject* python_type;

    static PyTypeObject* get_python_type()
    {
        return python_type;
    }
};

)";
        w.write(format, bind<write_type_name>(type));
    }

    void write_setup_filenames(writer& w, std::vector<std::string> const& namespaces)
    {
        XLANG_ASSERT(namespaces.size() > 0);

        for (auto&& ns : namespaces)
        {
            w.write("'%/src/py.%.cpp', ", settings.module, ns);
        }

        w.write("'%/src/_%.cpp'", settings.module, settings.module);
    }

    void write_struct_field_var_type(writer& w, Field const& field)
    {
        struct struct_field_type : public signature_handler_base<struct_field_type>
        {
            std::string field_type{};

            using signature_handler_base<struct_field_type>::handle;

            void handle(GenericTypeInstSig const& type) { handle(get_ireference_type(type)); }
            void handle_struct(TypeDef const& /*type*/) { field_type = "PyObject*"; }
            void handle(ElementType type) { field_type = writer::get_cpp_type(type); }
        };

        struct_field_type sft{};
        sft.handle(field.Signature().Type());
        w.write(sft.field_type);
    }

    void write_struct_field_initalizer(writer& w, Field const& field)
    {
        struct struct_field_initializer : public signature_handler_base<struct_field_initializer>
        {
            using signature_handler_base<struct_field_initializer>::handle;

            struct_field_initializer(writer& wr, std::string_view fn) : w(wr), field_name(fn) {}

            std::string_view field_name;
            writer& w;

            void handle_enum(TypeDef const& type)
            {
                w.write("static_cast<%>(_%)", type, field_name);
            }

            void handle_struct(TypeDef const& type)
            {
                w.write("py::converter<%>::convert_to(_%)", type, field_name);
            }

            void handle(GenericTypeInstSig const& type)
            {
                handle(get_ireference_type(type));
            }

            void handle(ElementType)
            {
                w.write("_%", field_name);
            }
        };

        struct_field_initializer initializer{ w, field.Name() };
        initializer.handle(field.Signature().Type());
    }

    void write_delegate_type_name(writer& w, TypeDef const& type)
    {
        w.write("::py@", type.TypeName());

        if (is_ptype(type))
        {
            w.write("<%>", bind_list<write_pinterface_type_arg_name>(", ", type.GenericParam()));
        }
    }

    void write_delegate_param(writer& w, method_signature::param_t const& p)
    {
        w.write("auto %", bind<write_param_name>(p));
    }

    void write_py_tuple_pack(writer& w, std::vector<std::string> const& params)
    {
        w.write("PyTuple_Pack(%, %)", static_cast<int>(params.size()), bind_list<write_detach_param>(", ", params));
    }

    void write_delegate_callable_wrapper(writer& w, TypeDef const& type)
    {
        auto guard{ w.push_generic_params(type.GenericParam()) };

        auto invoke = get_delegate_invoke(type);
        method_signature signature{ invoke };

        if (is_ptype(type))
        {
            w.write("\ntemplate <%>", bind_list<write_pinterface_type_arg>(", ", type.GenericParam()));
        }

        w.write("\nstruct py@\n{\n", type.TypeName());
        {
            writer::indent_guard g{ w };

            w.write("static % get(PyObject* callable)\n{\n", bind<write_full_type_name>(type));
            {
                writer::indent_guard gg{ w };

                auto format = R"(py::throw_if_pyobj_null(callable);

py::delegate_callable cb{ callable };

return [cb = std::move(cb)](%)
{
)";
                w.write(format, bind_list<write_delegate_param>(", ", signature.params()));

                {
                    writer::indent_guard ggg{ w };

                    w.write("winrt::handle_type<py::gil_state_traits> gil_state{ PyGILState_Ensure() };\n\n");

                    std::vector<std::string> tuple_params{};
                    for (auto&& p : signature.params())
                    {
                        auto param_name = w.write_temp("%", bind<write_param_name>(p));
                        auto py_param_name = "py_"s + param_name;

                        w.write("py::pyobj_handle %{ py::convert(%) };\n", py_param_name, param_name);
                        tuple_params.push_back(py_param_name);
                    }

                    if (tuple_params.size() > 0)
                    {
                        w.write("\npy::pyobj_handle args{ % };\n", bind<write_py_tuple_pack>(tuple_params));
                    }
                    else
                    {
                        w.write("py::pyobj_handle args{ nullptr };\n");
                    }

                    // TODO: check return from PyObject_CallObject for errors 
                    // https://docs.python.org/3.7/c-api/object.html?highlight=pyobject_callobject#c.PyObject_CallObject
                    if (signature.return_signature())
                    {
                        auto format2 = R"(
py::pyobj_handle return_value{ PyObject_CallObject(cb.callable(), args.get()) };
return py::convert<%>(return_value.get());
)";
                        w.write(format2, signature.return_signature().Type());
                    }
                    else
                    {
                        w.write("\nPyObject_CallObject(cb.callable(), args.get());\n");
                    }
                }

                w.write("};\n");
            }
            w.write("};\n");
        }
        w.write("};\n");
    }

    void write_pinterface_type_mapper(writer& w, TypeDef const& type)
    {
        if (!is_ptype(type))
            return;

        auto format = R"(template <%>
struct pinterface_python_type<%<%>>
{
    using abstract = ::py@;
    using concrete = ::py@Impl<%>;
};

)";
        w.write(format,
            bind_list<write_pinterface_type_arg>(", ", type.GenericParam()),
            type,
            bind_list<write_pinterface_type_arg_name>(", ", type.GenericParam()),
            type.TypeName(),
            type.TypeName(),
            bind_list<write_pinterface_type_arg_name>(", ", type.GenericParam()));
    }

    void write_delegate_type_mapper(writer& w, TypeDef const& type)
    {
        auto format = R"(template <%>
struct delegate_python_type<%>
{
    using type = %;
};

)";
        w.write(format,
            bind_list<write_pinterface_type_arg>(", ", type.GenericParam()),
            bind<write_full_type_name>(type),
            bind<write_delegate_type_name>(type));
    }

    void write_pinterface_param(writer& w, MethodDef const& method)
    {
        if (is_special(method))
        {
            if (!is_get_method(method))
            {
                w.write("PyObject* arg");
            }
        }
        else
        {
            w.write("PyObject* args");
        }
    }

    void write_pinterface_decl(writer& w, TypeDef const& type)
    {
        if (!is_ptype(type))
            return;

        auto guard{ w.push_generic_params(type.GenericParam()) };

        w.write("\nstruct py@\n{\n", type.TypeName());
        {
            writer::indent_guard g{ w };

            w.write("virtual ~py@() {};\n", type.TypeName());
            w.write("virtual winrt::Windows::Foundation::IUnknown const& get_unknown() = 0;\n");
            w.write("virtual std::size_t hash() = 0;\n");

            if (is_async_interface(type))
            {
                w.write("virtual PyObject* dunder_await() = 0;\n");
            }

            if (implements_iiterable(type) || implements_iiterator(type))
            {
                w.write("virtual PyObject* dunder_iter() = 0;\n");
            }

            if (implements_iiterator(type))
            {
                w.write("virtual PyObject* dunder_iternext() = 0;\n");
            }

            if (implements_sequence(type))
            {
                w.write("virtual Py_ssize_t _sq_length() = 0;\n");
                w.write("virtual PyObject* _sq_item(Py_ssize_t i) = 0;\n");

                if (implements_ivector(type))
                {
                    w.write("virtual int _sq_ass_item(Py_ssize_t i, PyObject* value) = 0;\n");
                }
            }

            if (implements_mapping(type))
            {
                w.write("virtual Py_ssize_t _mp_length() = 0;\n");
                w.write("virtual PyObject* _mp_subscript(PyObject* key) = 0;\n");

                if (implements_imap(type))
                {
                    w.write("virtual int _mp_ass_item(PyObject* key, PyObject* value) = 0;\n");
                }
            }

            w.write("\n");

            for (auto&&[name, overloads] : get_methods(type, true))
            {
                w.write("virtual % %(%) = 0;\n",
                    is_put_method(overloads.front().method) ? "int" : "PyObject*",
                    name,
                    bind<write_pinterface_param>(overloads.front().method));
            }
        }
        w.write("};\n");
    }

    void write_pinterface_impl(writer& w, TypeDef const& type)
    {
        if (!is_ptype(type))
            return;

        auto guard{ w.push_generic_params(type.GenericParam()) };

        w.write("\ntemplate<%>\nstruct py@Impl : public py@\n{\n", bind_list<write_pinterface_type_arg>(", ", type.GenericParam()), type.TypeName(), type.TypeName());

        {
            writer::indent_guard g{ w };

            w.write("py@Impl(%<%> o) : obj(o) {}\n", type.TypeName(), type, bind_list<write_pinterface_type_arg_name>(", ", type.GenericParam()));
            w.write("winrt::Windows::Foundation::IUnknown const& get_unknown() override { return obj; }\n");
            w.write("std::size_t hash() override { return py::get_instance_hash(obj); }\n");

            if (is_async_interface(type))
            {
                w.write("PyObject* dunder_await() override { return py::dunder_await(obj); }\n");
            }

            if (implements_iiterable(type))
            {
                w.write("\nPyObject* dunder_iter() override\n{\n");
                {
                    writer::indent_guard gg{ w };
                    write_try_catch(w, [](writer& w) { w.write("return py::convert(obj.First());\n"); });
                }

                w.write("}\n");
            }

            if (implements_iiterator(type))
            {
                w.write("PyObject* dunder_iter() override { return reinterpret_cast<PyObject*>(this); }\n");
                w.write("\nPyObject* dunder_iternext() override\n{\n");
                {
                    writer::indent_guard gg{ w };
                    write_try_catch(w, [](writer& w) {

                        w.write(R"(if (obj.HasCurrent())
{
    auto cur = obj.Current();
    obj.MoveNext();
    return py::convert(cur);
}
else
{
    return nullptr;
}
)");
                    });
                }
                w.write("}\n");
            }

            if (implements_sequence(type))
            {
                w.write("\nPy_ssize_t _sq_length() override\n{\n");
                {
                    writer::indent_guard gg{ w };

                    write_try_catch(w,
                        [](writer& w) { w.write("return static_cast<Py_ssize_t>(obj.Size());\n"); },
                        [](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                }
                w.write("}\n");

                w.write("\nPyObject* _sq_item(Py_ssize_t i) override\n{\n");
                {
                    writer::indent_guard gg{ w };

                    write_try_catch(w,
                        [](writer& w) { w.write("return py::convert(obj.GetAt(static_cast<uint32_t>(i)));\n"); });
                }
                w.write("}\n");

                if (implements_ivector(type))
                {
                    w.write("\nint _sq_ass_item(Py_ssize_t i, PyObject* value) override\n{\n");
                    {
                        writer::indent_guard gg{ w };

                        auto format = R"(if (value == nullptr) { obj.RemoveAt(static_cast<uint32_t>(i)); }
else { obj.SetAt(static_cast<uint32_t>(i), py::convert_to<T>(value)); }
return 0;
)";

                        write_try_catch(w,
                            [&format](writer& w) { w.write(format); },
                            [](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                    }
                    w.write("}\n");
                }
            }

            if (implements_mapping(type))
            {
                w.write("\nPy_ssize_t _mp_length() override\n{\n");
                {
                    writer::indent_guard gg{ w };

                    write_try_catch(w,
                        [](writer& w) { w.write("return static_cast<Py_ssize_t>(obj.Size());\n"); },
                        [](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                }
                w.write("}\n");

                w.write("\nPyObject* _mp_subscript(PyObject* key) override\n{\n");
                {
                    writer::indent_guard gg{ w };

                    write_try_catch(w,
                        [](writer& w) { w.write("return py::convert(obj.Lookup(py::convert_to<K>(key)));\n"); });
                }
                w.write("}\n");

                if (implements_imap(type))
                {
                    w.write("\nint _mp_ass_item(PyObject* key, PyObject* value) override\n{\n");
                    {
                        writer::indent_guard gg{ w };

                        write_try_catch(w,
                            [](writer& w) { w.write(R"(if (value == nullptr) { obj.Remove(py::convert_to<K>(key)); }
else { obj.Insert(py::convert_to<K>(key), py::convert_to<V>(value)); }
return 0;
)"); },
[](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                    }
                    w.write("}\n");
                }
            }

            w.write("\n");

            for (auto&& method : get_methods(type, true))
            {
                auto name = std::get<0>(method);
                auto overloads = std::get<1>(method);

                auto front_method = overloads.front().method;
                auto is_put = is_put_method(front_method);

                w.write("% %(%) override\n{\n",
                    is_put ? "int" : "PyObject*", name,
                    bind<write_pinterface_param>(front_method));
                {
                    writer::indent_guard gg{ w };

                    if (overloads.size() == 1 && (is_property_method(front_method) || is_event_method(front_method)))
                    {
                        write_try_catch(w,
                            [&](writer& w) { write_method_body(w, type, overloads.front()); },
                            is_put ? "-1" : "py::to_PyErr()");
                    }
                    else
                    {
                        write_method_overloads(w, type, overloads);
                    }

                }
                w.write("};\n\n");

            }

            w.write("\n%<%> obj{ nullptr };\n", type, bind_list<write_pinterface_type_arg_name>(", ", type.GenericParam()));
        }
        w.write("};\n");
    }

    void write_pinterface(writer& w, TypeDef const& type)
    {
        if (!is_ptype(type))
            return;

        write_pinterface_decl(w, type);
        write_pinterface_impl(w, type);
    }

    void write_dealloc_function(writer& w, TypeDef const& type)
    {
        if (!has_dealloc(type))
        {
            return;
        }

        auto format = R"(
static void @_dealloc(%* self)
{
    auto hash_value = %;
    py::wrapped_instance(hash_value, nullptr);
    self->obj%;
}
)";

        w.write(format,
            type.TypeName(),
            bind<write_wrapper_type>(type),
            is_ptype(type) ? "self->obj->hash()" : "std::hash<winrt::Windows::Foundation::IInspectable>{}(self->obj)",
            is_ptype(type) ? ".release()" : " = nullptr");
    }

    void write_out_param_init(writer& w, method_signature::param_t const& param)
    {
        struct out_param_init : public signature_handler_base<out_param_init>
        {
            std::string_view param_init{};

            using signature_handler_base<out_param_init>::handle;

            // WinRT class, interface and delegate out params must be initialized as nullptr
            void handle_class(TypeDef const& /*type*/) { param_init = "nullptr"; }
            void handle_delegate(TypeDef const& /*type*/) { param_init = "nullptr"; }
            void handle_interface(TypeDef const& /*type*/) { param_init = "nullptr"; }
            void handle(GenericTypeInstSig const& /*type*/) { param_init = "nullptr"; }

            // WinRT guid, struct and fundamental types don't require an initialization value
            void handle_guid(TypeRef const& /*type*/) { /* no init needed */ }
            void handle_struct(TypeDef const& /*type*/) { /* no init needed */ }
            void handle(ElementType /*type*/) { /* no init needed */ }
        };

        out_param_init initializer{};
        initializer.handle(param.second->Type());
        w.write(initializer.param_init);
    }

    void write_method_param_definition(writer& w, MethodDef const& method, method_signature::param_t const& param)
    {
        auto sequence = param.first.Sequence() - 1;

        switch (get_param_category(param))
        {
        case param_category::in:
            // if method is special (i.e. get/put/add/remove) but not RTSpecial (i.e. ctor)
            // treat the args value as a single value, not as a tuple
            if (method.SpecialName() && !method.Flags().RTSpecialName())
            {
                w.write("auto % = py::convert_to<%>(arg);\n", bind<write_param_name>(param), param.second->Type());
            }
            else
            {
                w.write("auto % = py::convert_to<%>(args, %);\n", bind<write_param_name>(param), param.second->Type(), sequence);
            }
            break;
        case param_category::out:
            w.write("% % { % };\n", param.second->Type(), bind<write_param_name>(param), bind<write_out_param_init>(param));
            break;
        case param_category::pass_array:
            w.write("auto _param% = py::convert_to<winrt::com_array<%>>(args, %);\n", sequence, param.second->Type(), sequence);
            w.write("auto param% = winrt::array_view<const %>(_param%.data(), _param%.data() + _param%.size());\n", sequence, param.second->Type(), sequence, sequence, sequence);
            break;
        case param_category::fill_array:
            w.write("auto %_count = py::convert_to<winrt::com_array<%>::size_type>(args, %);\n", bind<write_param_name>(param), param.second->Type(), sequence);
            w.write("winrt::com_array<%> % ( %_count, py::empty_instance<%>::get() );\n", param.second->Type(), bind<write_param_name>(param), bind<write_param_name>(param), param.second->Type());
            break;
        case param_category::receive_array:
            w.write("winrt::com_array<%> % { };\n", param.second->Type(), bind<write_param_name>(param));
            break;
        default:
            throw_invalid("invalid param_category");
        }
    }

    void write_method_invoke_context(writer& w, TypeDef const& type, MethodDef const& method)
    {
        if (is_ptype(type))
        {
            w.write("obj.");
        }
        else if (is_static(method))
        {
            w.write("%::", type);
        }
        else
        {
            w.write("self->obj.");
        }
    }

    void write_method_cpp_name(writer& w, MethodDef const& method)
    {
        auto name = method.Name();

        if (method.SpecialName())
        {
            w.write(name.substr(name.find('_') + 1));
        }
        else
        {
            w.write(name);
        }
    }

    void write_method_body(writer& w, TypeDef const& type, method_info const& info)
    {
        method_signature signature{ info.method };
        auto guard{ w.push_generic_params(info.type_arguments) };

        for (auto&& param : signature.params())
        {
            write_method_param_definition(w, info.method, param);
        }

        if (signature.params().size() > 0)
        {
            w.write("\n");
        }

        if (signature.return_signature())
        {
            w.register_type_namespace(signature.return_signature().Type());
            w.write("auto return_value = ");
        }
        w.write("%%(%);\n\n",
            bind<write_method_invoke_context>(type, info.method),
            bind<write_method_cpp_name>(info.method),
            bind_list<write_param_name>(", ", signature.params()));

        std::vector<std::string> return_values{};

        if (signature.return_signature())
        {
            
            auto format = R"(py::pyobj_handle out_return_value{ py::convert(return_value) };
if (!out_return_value) 
{ 
    return nullptr;
}

)";
            w.write(format);
            return_values.push_back("out_return_value");
        }

        for (auto&& param : signature.params())
        {
            if (!is_out_param(param))
            {
                continue;
            }

            auto sequence = param.first.Sequence() - 1;
            auto out_param = w.write_temp("out%", sequence);

            auto format = R"(py::pyobj_handle %{ py::convert(param%) };
if (!%) 
{
    return nullptr;
}

)";

            w.write(format, out_param, sequence, out_param);
            return_values.push_back(out_param);
        }

        if (return_values.size() == 0)
        {
            if (is_put_method(info.method))
            {
                w.write("return 0;\n");
            }
            else
            {
                w.write("Py_RETURN_NONE;\n");
            }
        }
        else if (return_values.size() == 1)
        {
            w.write("return %;\n", bind<write_detach_param>(return_values[0]));
        }
        else
        {
            auto x = w.write_temp("%", std::to_string(return_values.size()));
            w.write("return PyTuple_Pack(%, %);\n", x, bind_list<write_detach_param>(", ", return_values));
        }
    }

    void write_method_overloads(writer& w, TypeDef const& type, std::vector<xlang::method_info> const& overloads)
    {
        w.write("Py_ssize_t arg_count = PyTuple_Size(args);\n\n");

        separator s{ w, "else " };

        for (auto&& overload : overloads)
        {
            method_signature signature{ overload.method };

            s();
            w.write("if (arg_count == %)\n{\n", count_in_param(signature.params()));
            {
                writer::indent_guard gg{ w };

                write_try_catch(w, [&](writer& w) { write_method_body(w, type, overload); });
            }
            w.write("}\n");
        }

        w.write(R"(else if (arg_count != -1)
{
    PyErr_SetString(PyExc_TypeError, "Invalid parameter count");
}

return nullptr;
)");
    }

    void write_method_self_type(writer& w, TypeDef const& type, MethodDef const& method)
    {
        if (is_static(method))
        {
            w.write("PyObject* /*unused*/");
        }
        else
        {
            w.write("%* self", bind<write_wrapper_type>(type));
        }
    }

    void write_method_arg_param_name(writer& w, MethodDef const& method)
    {
        if (is_special(method))
        {
            if (is_get_method(method))
            {
                w.write("/*unused*/");
            }
            else
            {
                w.write("arg");
            }
        }
        else
        {
            w.write("args");
        }
    }

    void write_method_function(writer& w, TypeDef const& type, std::string_view const& method_name, std::vector<method_info> const& overloads)
    {
        w.write("\nstatic PyObject* @_%(%, PyObject* %)\n{\n",
            type.TypeName(),
            method_name,
            bind<write_method_self_type>(type, overloads.front().method),
            bind<write_method_arg_param_name>(overloads.front().method));

        {
            writer::indent_guard g{ w };

            if (is_ptype(type))
            {
                w.write("return self->obj->%(%);\n", method_name, bind<write_method_arg_param_name>(overloads.front().method));
            }
            else
            {
                write_method_overloads(w, type, overloads);
            }
        }
        w.write("}\n");
    }

    void write_get_property_function(writer& w, TypeDef const& type, MethodDef const& get_method, std::vector<std::string> const& type_arguments)
    {
        w.write("\nstatic PyObject* @_%(%, %* /*unused*/)\n{\n",
            type.TypeName(),
            get_method.Name(),
            bind<write_method_self_type>(type, get_method),
            is_static(get_method) ? "PyObject" : "void");

        {
            writer::indent_guard g{ w };

            if (is_ptype(type))
            {
                w.write("return self->obj->%(%);\n", get_method.Name(), bind<write_method_arg_param_name>(get_method));
            }
            else
            {
                write_try_catch(w, [&](writer& w) { write_method_body(w, type, { get_method, type_arguments }); });
            }
        }

        w.write("}\n");
    }

    void write_set_property_function(writer& w, TypeDef const& type, MethodDef const& set_method, std::vector<std::string> const& type_arguments)
    {
        if (is_static(set_method))
        {
            w.write("\nstatic PyObject* @_%(%, PyObject* %)\n{\n",
                type.TypeName(),
                set_method.Name(),
                bind<write_method_self_type>(type, set_method),
                bind<write_method_arg_param_name>(set_method));

            {
                writer::indent_guard g{ w };
                write_try_catch(w, [&](writer& w) { write_method_body(w, type, { set_method , type_arguments }); });
            }

            w.write("}\n");
        }
        else
        {
            w.write("\nstatic int @_%(%, PyObject* %, void* /*unused*/)\n{\n",
                type.TypeName(),
                set_method.Name(),
                bind<write_method_self_type>(type, set_method),
                bind<write_method_arg_param_name>(set_method));

            {
                writer::indent_guard g{ w };

                w.write(R"(if (% == nullptr)
{
    PyErr_SetString(PyExc_TypeError, "property delete not supported");
    return -1;
}

)", bind<write_method_arg_param_name>(set_method));


                if (is_ptype(type))
                {
                    w.write("return self->obj->%(%);\n", set_method.Name(), bind<write_method_arg_param_name>(set_method));
                }
                else
                {
                    write_try_catch(w, [&](writer& w) { write_method_body(w, type, { set_method, type_arguments }); }, "-1");
                }
            }

            w.write("}\n");
        }
    }

    void write_event_functions(writer& w, TypeDef const& type, event_info const& evt)
    {
        auto methods = get_event_methods(evt.event);

        w.write("\nstatic PyObject* @_%(%* self, PyObject* arg)\n{\n", type.TypeName(), methods.add.Name(), bind<write_wrapper_type>(type));
        {
            writer::indent_guard g{ w };
            if (is_ptype(type))
            {
                w.write("return self->obj->%(%);\n", methods.add.Name(), bind<write_method_arg_param_name>(methods.add));
            }
            else
            {
                write_try_catch(w, [&](writer& w) { write_method_body(w, type, { methods.add, evt.type_arguments }); });
            }
        }
        w.write("}\n");

        w.write("\nstatic PyObject* @_%(%* self, PyObject* arg)\n{\n", type.TypeName(), methods.remove.Name(), bind<write_wrapper_type>(type));
        {
            writer::indent_guard g{ w };
            if (is_ptype(type))
            {
                w.write("return self->obj->%(%);\n", methods.remove.Name(), bind<write_method_arg_param_name>(methods.remove));
            }
            else
            {
                write_try_catch(w, [&](writer& w) { write_method_body(w, type, { methods.remove, evt.type_arguments }); });
            }
        }
        w.write("}\n");
    }

    void write_from_function(writer& w, TypeDef const& type)
    {
        auto format = R"(
static PyObject* __%_from(PyObject* /*unused*/, PyObject* arg)
{
    try
    {
        auto instance = py::converter<winrt::Windows::Foundation::IInspectable>::convert_to(arg);
        return py::convert(instance.as<%>());
    }
    catch (...)
    {
        return py::to_PyErr();
    }
}
)";

        w.write(format, type.TypeName(), type);
    }

    void write_str_function(writer& w, TypeDef const& type)
    {
        w.write("\nstatic PyObject* __@_str(%* self)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
        {
            writer::indent_guard g{ w };
            write_try_catch(w, [](auto& w) { w.write("return py::convert(self->obj.ToString());\n"); });
        }
        w.write("}\n");
    }

    void write_enter_exit_functions(writer& w, TypeDef const& type)
    {
        auto format = R"(
static PyObject* __@_enter(%* self)
{
    Py_INCREF(self);
    return (PyObject*)self;
}

static PyObject* __@_exit(%* self)
{
)";
        w.write(format,
            type.TypeName(), bind<write_wrapper_type>(type),
            type.TypeName(), bind<write_wrapper_type>(type));
        {
            writer::indent_guard g{ w };
            write_try_catch(w, [](auto& w) { w.write("self->obj.Close();\nPy_RETURN_FALSE;\n"); });

        }
        w.write("}\n");
    }

    void write_await_function(writer& w, TypeDef const& type)
    {
        w.write("\nstatic PyObject* __@_await(%* self)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
        {
            writer::indent_guard g{ w };

            if (is_ptype(type))
            {
                w.write("return self->obj->dunder_await();\n");
            }
            else
            {
                w.write("return py::dunder_await(self->obj);\n");
            }
        }
        w.write("}\n");
    }

    void write_method_functions(writer& w, TypeDef const& type)
    {
        for (auto&&[name, overloads] : get_methods(type))
        {
            write_method_function(w, type, name, overloads);
        }

        for (auto&& prop : get_properties(type))
        {
            auto methods = get_property_methods(prop.property);

            write_get_property_function(w, type, methods.get, prop.type_arguments);

            if (methods.set)
            {
                write_set_property_function(w, type, methods.set, prop.type_arguments);
            }
        }

        for (auto&& evt : get_events(type))
        {
            write_event_functions(w, type, evt);
        }

        // TODO: pinterface _from support
        if (!is_ptype(type) && !is_static(type))
        {
            write_from_function(w, type);
        }

        if (implements_istringable(type))
        {
            write_str_function(w, type);
        }

        if (implements_iclosable(type))
        {
            write_enter_exit_functions(w, type);
        }

        if (is_async_interface(type))
        {
            write_await_function(w, type);
        }

        if (implements_iiterable(type))
        {
            w.write("\nstatic PyObject* __@_iter(%* self)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
            {
                writer::indent_guard g{ w };

                if (is_ptype(type))
                {
                    w.write("return self->obj->dunder_iter();\n");
                }
                else
                {
                    write_try_catch(w, [](auto& w) { w.write("return py::convert(self->obj.First());\n"); });
                }
            }
            w.write("}\n");
        }

        if (implements_iiterator(type))
        {
            w.write("\nstatic PyObject* __@_iter(%* self)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
            {
                writer::indent_guard g{ w };
                write_try_catch(w, [](auto& w) { w.write("return reinterpret_cast<PyObject*>(self);\n"); });
            }
            w.write("}\n");

            w.write("\nstatic PyObject* __@_iternext(%* self)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
            {
                writer::indent_guard g{ w };

                w.write("return self->obj->dunder_iternext();\n");
            }
            w.write("}\n");
        }

        if (implements_sequence(type))
        {
            auto type_args = get_type_arguments(type, "Windows.Foundation.Collections", { "IVector`1", "IVectorView`1" });
            auto col_type = type_args.at(0);

            w.write("\nstatic Py_ssize_t __@_sq_length(%* self)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
            {
                writer::indent_guard gg{ w };

                if (is_ptype(type))
                {
                    w.write("return self->obj->_sq_length();\n");
                }
                else
                {
                    write_try_catch(w,
                        [](writer& w) { w.write("return static_cast<Py_ssize_t>(self->obj.Size());\n"); },
                        [](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                }
            }
            w.write("}\n");

            w.write("\nstatic PyObject* __@_sq_item(%* self, Py_ssize_t i)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
            {
                writer::indent_guard gg{ w };

                if (is_ptype(type))
                {
                    w.write("return self->obj->_sq_item(i);\n");
                }
                else
                {
                    write_try_catch(w,
                        [](writer& w) { w.write("return py::convert(self->obj.GetAt(static_cast<uint32_t>(i)));\n"); });
                }
            }
            w.write("}\n");

            if (implements_ivector(type))
            {
                w.write("\nstatic int __@_sq_ass_item(%* self, Py_ssize_t i, PyObject* value)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
                {
                    writer::indent_guard gg{ w };

                    if (is_ptype(type))
                    {
                        w.write("return self->obj->_sq_ass_item(i, value);\n");
                    }
                    else
                    {
                        write_try_catch(w,
                            [&col_type](writer& w)
                        {
                            auto format = R"(if (value == nullptr) { self->obj.RemoveAt(static_cast<uint32_t>(i)); }
 else { self->obj.SetAt(static_cast<uint32_t>(i), py::convert_to<%>(value)); }
 return 0;
 )";
                            w.write(format, col_type);
                        },
                            [](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                    }
                }
                w.write("}\n");
            }
        }

        if (implements_mapping(type))
        {
            auto type_args = get_type_arguments(type, "Windows.Foundation.Collections", { "IMap`2", "IMapView`2" });
            auto key_type = type_args.at(0);
            auto value_type = type_args.at(1);


            w.write("\nstatic Py_ssize_t __@_mp_length(%* self)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
            {
                writer::indent_guard gg{ w };

                if (is_ptype(type))
                {
                    w.write("return self->obj->_mp_length();\n");
                }
                else
                {
                    write_try_catch(w,
                        [](writer& w) { w.write("return static_cast<Py_ssize_t>(self->obj.Size());\n"); },
                        [](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                }
            }
            w.write("}\n");

            w.write("\nstatic PyObject* __@_mp_subscript(%* self, PyObject* key)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
            {
                writer::indent_guard gg{ w };

                if (is_ptype(type))
                {
                    w.write("return self->obj->_mp_subscript(key);\n");
                }
                else
                {
                    write_try_catch(w,
                        [&key_type](writer& w) { w.write("return py::convert(self->obj.Lookup(py::convert_to<%>(key)));\n", key_type); });
                }
            }
            w.write("}\n");

            if (implements_imap(type))
            {
                w.write("\nstatic int __@_mp_ass_item(%* self, PyObject* key, PyObject* value)\n{\n", type.TypeName(), bind<write_wrapper_type>(type));
                {
                    writer::indent_guard gg{ w };

                    if (is_ptype(type))
                    {
                        w.write("return self->obj->_mp_ass_item(key, value);\n");
                    }
                    else
                    {
                        write_try_catch(w,
                            [&key_type, &value_type](writer& w)
                        {
                            auto format = R"(if (value == nullptr) { self->obj.Remove(py::convert_to<%>(key)); }
else { self->obj.Insert(py::convert_to<%>(key), py::convert_to<%>(value)); }
return 0;
)";
                            w.write(format, key_type, key_type, value_type);
                        },
                            [](writer& w) { w.write("py::to_PyErr();\nreturn -1;\n"); });
                    }
                }
                w.write("}\n");
            }
        }
    }

    void write_class_new_function_overload(writer& w, MethodDef const& method, method_signature const& signature)
    {
        for (auto&& param : signature.params())
        {
            write_method_param_definition(w, method, param);
        }

        if (signature.params().size() > 0)
        {
            w.write("\n");
        }

        w.write("% instance{ % };\nreturn py::wrap(instance, type);\n",
            method.Parent(),
            bind_list<write_param_name>(", ", signature.params()));
    }

    void write_class_new_function(writer& w, TypeDef const& type)
    {
        w.write("\nstatic PyObject* %_new(PyTypeObject* type, PyObject* args, PyObject* kwds)\n{\n", type.TypeName());

        {
            writer::indent_guard g{ w };

            auto constructors = get_constructors(type);
            if (is_static(type) || constructors.size() == 0)
            {
                auto format = R"(PyErr_SetString(PyExc_TypeError, "% is not activatable");
return nullptr;
)";
                w.write(format, type.TypeName());
            }
            else
            {
                w.write(R"(if (kwds != nullptr)
{
    PyErr_SetString(PyExc_TypeError, "keyword arguments not supported");
    return nullptr;
}

Py_ssize_t arg_count = PyTuple_Size(args);

)");

                separator s{ w, "else " };

                for (auto&& ctor : constructors)
                {
                    method_signature signature{ ctor };

                    s();
                    w.write("if (arg_count == %)\n{\n", count_in_param(signature.params()));
                    {
                        writer::indent_guard g2{ w };
                        write_try_catch(w, [&](writer& w) { write_class_new_function_overload(w, ctor, signature); });
                    }
                    w.write("}\n");
                }

                w.write(R"(else if (arg_count != -1)
{
    PyErr_SetString(PyExc_TypeError, "Invalid parameter count");
}

return nullptr;
)");
            }
        }
        w.write("}\n");
    }

    void write_class(writer& w, TypeDef const& type)
    {
        auto guard{ w.push_generic_params(type.GenericParam()) };

        w.write("\n// ----- % class --------------------\n", type.TypeName());
        write_winrt_type_specialization_storage(w, type);
        write_class_new_function(w, type);
        write_dealloc_function(w, type);
        write_method_functions(w, type);
        write_method_table(w, type);
        write_getset_table(w, type);
        write_type_slot_table(w, type);
        write_type_spec(w, type);
    }

    void write_interface_new_function(writer& w, TypeDef const& type)
    {
        XLANG_ASSERT(get_category(type) == category::interface_type);

        auto format = R"(
PyObject* @_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    PyErr_SetString(PyExc_TypeError, "@ interface is not activatable");
    return nullptr;
}
)";
        w.write(format, type.TypeName(), type.TypeName());
    }

    void write_interface(writer& w, TypeDef const& type)
    {
        if (is_exclusive_to(type))
        {
            return;
        }

        auto guard{ w.push_generic_params(type.GenericParam()) };

        w.write("\n// ----- @ interface --------------------\n", type.TypeName());
        write_winrt_type_specialization_storage(w, type);
        write_interface_new_function(w, type);
        write_dealloc_function(w, type);
        write_method_functions(w, type);
        write_method_table(w, type);
        write_getset_table(w, type);
        write_type_slot_table(w, type);
        write_type_spec(w, type);
    }

    void write_struct_converter_decl(writer& w, TypeDef const& type)
    {
        w.write("template<>\nstruct converter<%>\n{\n", type);
        {
            writer::indent_guard g{ w };
            w.write("static PyObject* convert(% instance) noexcept;\nstatic % convert_to(PyObject* obj);\n", type, type);
        }

        w.write("};\n\n");
    }

    void write_struct_field_name(writer& w, TypeDef const& type, Field const& field)
    {
        if (type.TypeNamespace() == "Windows.Foundation.Numerics")
        {
            static const std::set<std::string_view> custom_numerics = { "Matrix3x2", "Matrix4x4", "Plane", "Quaternion", "Vector2", "Vector3", "Vector4" };

            if (custom_numerics.find(type.TypeName()) != custom_numerics.end())
            {
                for (char c : field.Name())
                {
                    w.write(static_cast<char>(::tolower(c)));
                }
                return;
            }
        }

        w.write(field.Name());
    }

    void write_struct_field_keyword(writer& w, Field const& field)
    {
        w.write("\"%\", ", field.Name());
    }

    void write_struct_field_format(writer& w, Field const& field)
    {
        struct struct_format_field : public signature_handler_base<struct_format_field>
        {
            using signature_handler_base<struct_format_field>::handle;

            std::string_view format_string{};

            void handle_struct(TypeDef const& /*type*/) { format_string = "O"; }
            //TODO: make IReference fields optional
            void handle(GenericTypeInstSig const& type) { handle(get_ireference_type(type)); }

            void handle(ElementType type)
            {
                switch (type)
                {
                case ElementType::Boolean:
                    format_string = "p";
                    break;
                    // TODO: 'u' format string was deprecated in Python 3.3. Need to move to a supported construct
                case ElementType::Char:
                    format_string = "u1";
                    break;
                case ElementType::I1:
                    format_string = "y1";
                    break;
                case ElementType::U1:
                    format_string = "y1";
                    break;
                case ElementType::I2:
                    format_string = "h";
                    break;
                case ElementType::U2:
                    format_string = "H";
                    break;
                case ElementType::I4:
                    format_string = "i";
                    break;
                case ElementType::U4:
                    format_string = "I";
                    break;
                case ElementType::I8:
                    format_string = "L";
                    break;
                case ElementType::U8:
                    format_string = "K";
                    break;
                case ElementType::R4:
                    format_string = "f";
                    break;
                case ElementType::R8:
                    format_string = "d";
                    break;
                    // TODO: 'u' format string was deprecated in Python 3.3. Need to move to a supported construct
                case ElementType::String:
                    format_string = "u";
                    break;
                case ElementType::Object:
                    throw_invalid("structs cannot contain ElementType::Object");
                default:
                    throw_invalid("write_struct_field_format element type not impl");
                }
            }
        };

        struct_format_field field_format{};
        field_format.handle(field.Signature().Type());
        w.write(field_format.format_string);
    }

    void write_struct_field_parameter(writer& w, Field const& field)
    {
        w.write(", &_%", field.Name());
    }

    void write_struct_constructor(writer& w, TypeDef const& type)
    {
        w.write("PyObject* %_new(PyTypeObject* type, PyObject* args, PyObject* kwds)\n{\n", type.TypeName());
        {
            writer::indent_guard g{ w };

            w.write("auto tuple_size = PyTuple_Size(args);\n\nif ((tuple_size == 0) && (kwds == nullptr))\n{\n");
            {
                writer::indent_guard gg{ w };
                write_try_catch(w, [&type](writer& w) { w.write("% instance{};\nreturn py::wrap_struct(instance, type);\n", type); });
            }
            w.write("};\n\nif ((tuple_size == 1) && (kwds == nullptr))\n{\n");

            {
                writer::indent_guard gg{ w };
                w.write("auto arg = PyTuple_GetItem(args, 0);\nif (PyDict_Check(arg))\n{\n");

                {
                    writer::indent_guard ggg{ w };
                    write_try_catch(w, [&type](writer& w) { w.write("auto instance = py::converter<%>::convert_to(arg);\nreturn py::wrap_struct(instance, type);\n", type); });
                }
                w.write("};\n");
            }
            w.write("};\n\n");

            for (auto&& field : type.FieldList())
            {
                w.write("% _%{};\n", bind<write_struct_field_var_type>(field), field.Name());
            }

            w.write("\nstatic char* kwlist[] = {%nullptr};\n", bind_each<write_struct_field_keyword>(type.FieldList()));

            w.write("if (!PyArg_ParseTupleAndKeywords(args, kwds, \"%\", kwlist%))\n{\n    return nullptr;\n}\n\n",
                bind_each<write_struct_field_format>(type.FieldList()),
                bind_each<write_struct_field_parameter>(type.FieldList()));

            write_try_catch(w, [&type](writer& w)
            {
                if (is_customized_struct(type))
                {
                    w.write("% instance{ };\ncustom_set(instance, %);\n", type, bind<write_struct_field_initalizer>(type.FieldList().first));
                }
                else
                {
                    w.write("% instance{ % };\n",
                        type,
                        bind_list<write_struct_field_initalizer>(", ", type.FieldList()));
                }

                w.write("return py::wrap_struct(instance, type);\n");
            });
        }
        w.write("}\n");
    }

    void write_struct_convert_functions(writer& w, TypeDef const& type)
    {
        w.write("\nPyObject* py::converter<%>::convert(% instance) noexcept\n{\n", type, type);
        {
            writer::indent_guard g{ w };
            w.write("return py::wrap_struct<%>(instance, py::get_python_type<%>());\n", type, type);
        }
        w.write("}\n\n");

        w.write("% py::converter<%>::convert_to(PyObject* obj)\n{\n", type, type);
        {
            writer::indent_guard g{ w };

            auto format = R"(throw_if_pyobj_null(obj);

if (Py_TYPE(obj) == py::get_python_type<%>())
{
    return reinterpret_cast<py::winrt_struct_wrapper<%>*>(obj)->obj;
}

if (!PyDict_Check(obj))
{
    throw winrt::hresult_invalid_argument();
}

)";
            w.write(format, type, type);

            w.write("% new_value{};\n", type);

            for (auto&& field : type.FieldList())
            {
                auto field_name = w.write_temp("%", bind<write_struct_field_name>(type, field));

                w.write("\nPyObject* py_% = PyDict_GetItemString(obj, \"%\");\n", field_name, field.Name());
                w.write("if (!py_%) { throw winrt::hresult_invalid_argument(); }\n", field_name);

                if (is_customized_struct(type))
                {
                    w.write("custom_set(new_value, converter<%>::convert_to(py_%));\n", field.Signature().Type(), field_name);
                }
                else
                {
                    w.write("new_value.% = converter<%>::convert_to(py_%);\n", field_name, field.Signature().Type(), field_name);
                }

            }

            w.write("\nreturn new_value;\n");
        }
        w.write("}\n\n");
    }

    void write_struct_property_function(writer& w, TypeDef const& type, Field const& field)
    {
        w.write("\nstatic PyObject* @_get_%(%* self, void* /*unused*/)\n{\n",
            type.TypeName(),
            field.Name(),
            bind<write_wrapper_type>(type));
        {
            writer::indent_guard g{ w };
            write_try_catch(w, [&type, &field](writer& w)
            {
                if (is_customized_struct(type))
                {
                    w.write("return py::convert(custom_get(self->obj));\n");
                }
                else
                {
                    w.write("return py::convert(self->obj.%);\n", bind<write_struct_field_name>(type, field));
                }
            });
        }
        w.write("}\n");

        w.write("\nstatic int @_set_%(%* self, PyObject* value, void* /*unused*/)\n{\n",
            type.TypeName(),
            field.Name(),
            bind<write_wrapper_type>(type));
        {
            writer::indent_guard g{ w };

            w.write(R"(if (value == nullptr)
{
    PyErr_SetString(PyExc_TypeError, "property delete not supported");
    return -1;
}

)");

            write_try_catch(w, [&type, &field](writer& w)
            {
                if (is_customized_struct(type))
                {
                    w.write("custom_set(self->obj, py::converter<%>::convert_to(value));\n", field.Signature().Type());
                }
                else
                {
                    w.write("self->obj.% = py::converter<%>::convert_to(value);\n", bind<write_struct_field_name>(type, field), field.Signature().Type());
                }

                w.write("return 0;\n");
            }, "-1");
        }
        w.write("}\n");
    }

    void write_property_functions(writer& w, TypeDef const& type)
    {
        for (auto&& field : type.FieldList())
        {
            write_struct_property_function(w, type, field);
        }
    }

    void write_struct_customization(writer& w, TypeDef const& type)
    {
        if (type.TypeNamespace() == "Windows.Foundation")
        {
            auto type_name = type.TypeName();

            if (type_name == "DateTime")
            {
                w.write(R"(
inline int64_t custom_get(winrt::Windows::Foundation::DateTime const& instance) 
{
    return instance.time_since_epoch().count();
}

inline void custom_set(winrt::Windows::Foundation::DateTime& instance, int64_t value) 
{
    instance = winrt::Windows::Foundation::DateTime{ winrt::Windows::Foundation::TimeSpan{ value } };
}
)");
            }
            else if (type_name == "TimeSpan")
            {
                w.write(R"(
inline int64_t custom_get(winrt::Windows::Foundation::TimeSpan const& instance) 
{
    return instance.count();
}

inline void custom_set(winrt::Windows::Foundation::TimeSpan& instance, int64_t value) 
{
    instance = winrt::Windows::Foundation::TimeSpan{ value };
}
)");
            }
            else if (type_name == "EventRegistrationToken")
            {
                w.write(R"(
inline int64_t custom_get(winrt::event_token const& instance) 
{
    return instance.value;
}

inline void custom_set(winrt::event_token& instance, int64_t value) 
{
    instance.value = value;
}
)");
            }
            else if (type_name == "HResult")
            {
                w.write(R"(
inline int32_t custom_get(winrt::hresult const& instance) 
{
    return instance;
}

inline void custom_set(winrt::hresult& instance, int32_t value) 
{
    instance = value;
}
)");
            }
        }
    }

    void write_struct_dealloc(writer& w, TypeDef const& type)
    {
        w.write(R"(
static void @_dealloc(PyObject*)
{
}
)", type.TypeName());
    }

    void write_struct(writer& w, TypeDef const& type)
    {
        auto guard{ w.push_generic_params(type.GenericParam()) };

        w.write("\n// ----- % struct --------------------\n", type.TypeName());
        write_struct_customization(w, type);
        write_winrt_type_specialization_storage(w, type);
        write_struct_convert_functions(w, type);
        write_struct_constructor(w, type);
        write_struct_dealloc(w, type);
        write_property_functions(w, type);
        write_getset_table(w, type);
        write_type_slot_table(w, type);
        write_type_spec(w, type);
    }

    void write_license_text(writer& w)
    {
        // TODO: version stamp
        w.write("WARNING: Please don't edit this file. It was generated by Python/WinRT");
    }

    void write_license_python(writer& w)
    {
        w.write("# %\n\n", bind<write_license_text>());
    }

    void write_license_cpp(writer& w)
    {
        w.write("// %\n\n", bind<write_license_text>());
    }

    void write_winrt_type_specialization_storage(writer& w, TypeDef const& type)
    {
        w.write("\nPyTypeObject* py::winrt_type<%>::python_type;\n", bind<write_type_name>(type));
    }

    void write_getset_table(writer& w, TypeDef const& type)
    {
        w.write("\nstatic PyGetSetDef @_getset[] = {\n", type.TypeName());

        {
            writer::indent_guard g{ w };
            auto category = get_category(type);

            if (category == category::class_type || category == category::interface_type)
            {
                for (auto&& prop : get_properties(type))
                {
                    if (is_static(prop))
                    {
                        continue;
                    }

                    auto methods = get_property_methods(prop.property);
                    auto setter = methods.set ? w.write_temp("(setter)@_%", type.TypeName(), methods.set.Name()) : "nullptr"s;

                    // TODO: remove const_cast once pywinrt is updated to target Python 3.7. 
                    //       pywinrt currently targeting 3.6 because that's the version that ships with VS 2017 v15.8
                    //       https://github.com/python/cpython/commit/007d7ff73f4e6e65cfafd512f9c23f7b7119b803
                    w.write("{ const_cast<char*>(\"%\"), (getter)@_%, %, nullptr, nullptr },\n",
                        prop.property.Name(),
                        type.TypeName(), methods.get.Name(),
                        setter);
                }
            }

            if (category == category::struct_type)
            {
                for (auto&& field : type.FieldList())
                {
                    // TODO: remove const_cast once pywinrt is updated to target Python 3.7. 
                    //       pywinrt currently targeting 3.6 because that's the version that ships with VS 2017 v15.8
                    //       https://github.com/python/cpython/commit/007d7ff73f4e6e65cfafd512f9c23f7b7119b803
                    w.write("{ const_cast<char*>(\"%\"), (getter)@_get_%, (setter)@_set_%, nullptr, nullptr },\n",
                        field.Name(),
                        type.TypeName(), field.Name(),
                        type.TypeName(), field.Name());
                }
            }
            w.write("{ nullptr }\n");
        }

        w.write("};\n");
    }

    void write_method_table_flags(writer& w, MethodDef const& method)
    {
        if (is_get_method(method))
        {
            w.write("METH_NOARGS");
        }
        else if (is_put_method(method) || is_add_method(method) || is_remove_method(method))
        {
            w.write("METH_O");
        }
        else
        {
            w.write("METH_VARARGS");
        }

        if (is_static(method))
        {
            w.write(" | METH_STATIC");
        }
    }

    void write_method_table_record(writer& w, TypeDef const& type, MethodDef const& method)
    {
        w.write("{ \"%\", (PyCFunction)@_%, %, nullptr },\n",
            method.Name(), type.TypeName(), method.Name(), bind<write_method_table_flags>(method));
    }

    void write_method_table(writer& w, TypeDef const& type)
    {
        XLANG_ASSERT(get_category(type) == category::interface_type || get_category(type) == category::class_type);

        w.write("\nstatic PyMethodDef @_methods[] = {\n", type.TypeName());

        {
            writer::indent_guard g{ w };

            for (auto&&[name, overloads] : get_methods(type))
            {
                write_method_table_record(w, type, overloads.front().method);
            }

            for (auto&& prop : get_properties(type))
            {
                if (!is_static(prop))
                {
                    continue;
                }

                auto prop_methods = get_property_methods(prop.property);
                write_method_table_record(w, type, prop_methods.get);

                if (prop_methods.set)
                {
                    write_method_table_record(w, type, prop_methods.set);
                }
            }

            for (auto&& evt : get_events(type))
            {
                auto event_methods = get_event_methods(evt.event);
                write_method_table_record(w, type, event_methods.add);
                write_method_table_record(w, type, event_methods.remove);
            }

            if (!(is_ptype(type) || is_static(type)))
            {
                w.write("{ \"_from\", (PyCFunction)__@_from, METH_O | METH_STATIC, nullptr },\n", type.TypeName());
            }

            if (implements_iclosable(type))
            {
                w.write("{ \"__enter__\", (PyCFunction)__@_enter, METH_O, nullptr },\n", type.TypeName());
                w.write("{ \"__exit__\",  (PyCFunction)__@_exit,  METH_O, nullptr },\n", type.TypeName());
            }

            w.write("{ nullptr }\n");
        }

        w.write("};\n");
    }

    void write_type_slot_table(writer& w, TypeDef const& type)
    {
        auto category = get_category(type);

        XLANG_ASSERT((category == category::class_type)
            || (category == category::interface_type)
            || (category == category::struct_type));

        auto name = type.TypeName();

        w.write("\nstatic PyType_Slot @_Type_slots[] = \n{\n", name);

        {
            writer::indent_guard g{ w };

            if (has_dealloc(type))
            {
                w.write("{ Py_tp_dealloc, @_dealloc },\n", name);
            }

            w.write("{ Py_tp_new, @_new },\n", name);

            if ((category == category::class_type) || (category == category::interface_type))
            {
                w.write("{ Py_tp_methods, @_methods },\n", name);
            }

            w.write("{ Py_tp_getset, @_getset },\n", name);

            if (implements_istringable(type))
            {
                w.write("{ Py_tp_str, __@_str },\n", name);
            }

            if (is_async_interface(type))
            {
                w.write("{ Py_am_await, (unaryfunc)__@_await },\n", name);
            }

            XLANG_ASSERT(!(implements_iiterable(type) && implements_iiterator(type)));
            if (implements_iiterable(type) || implements_iiterator(type))
            {
                w.write("{ Py_tp_iter, __@_iter },\n", name);
            }

            if (implements_iiterator(type))
            {
                w.write("{ Py_tp_iternext, __@_iternext },\n", name);
            }

            if (implements_sequence(type))
            {
                w.write("{ Py_sq_length, __@_sq_length },\n", name);
                w.write("{ Py_sq_item, __@_sq_item },\n", name);

                if (implements_ivector(type))
                {
                    w.write("{ Py_sq_ass_item, __@_sq_ass_item },\n", name);
                }
            }

            if (implements_mapping(type))
            {
                w.write("{ Py_mp_length, __@_mp_length },\n", name);
                w.write("{ Py_mp_subscript, __@_mp_subscript },\n", name);

                if (implements_imap(type))
                {
                    w.write("{ Py_mp_ass_subscript, __@_mp_ass_item },\n", name);
                }
            }

            w.write("{ 0, nullptr },\n");
        }

        w.write("};\n");
    }

    void write_type_spec_size(writer& w, TypeDef const& type)
    {
        auto category = get_category(type);

        XLANG_ASSERT((category == category::class_type)
            || (category == category::interface_type)
            || (category == category::struct_type));

        if (is_static(type))
        {
            w.write("0");
        }
        else
        {
            w.write("sizeof(%)", bind<write_wrapper_type>(type));
        }
    }

    void write_module_name(writer& w, std::string_view const& ns)
    {
        auto segments = get_dotted_name_segments(ns);
        w.write("_%_%", settings.module, bind_list("_", segments));
    }

    void write_type_spec(writer& w, TypeDef const& type)
    {
        auto format = R"(
static PyType_Spec @_Type_spec =
{
    "%.@",
    %,
    0,
    Py_TPFLAGS_DEFAULT,
    @_Type_slots
};
)";
        auto type_name = type.TypeName();
        w.write(format,
            type_name,
            bind<write_module_name>(type.TypeNamespace()),
            type_name,
            bind<write_type_spec_size>(type),
            type_name);
    }


    void write_namespace_module_exec_init_python_type(writer& w, TypeDef const& type)
    {
        if (is_exclusive_to(type))
        {
            return;
        }

        auto winrt_type_param = is_ptype(type)
            ? w.write_temp("py@", type.TypeName())
            : w.write_temp("%", type);

        w.write("\n{\n");

        {
            writer::indent_guard g{ w };

            w.write("py::pyobj_handle type_object { ");
            if (has_dealloc(type))
            {
                w.write("PyType_FromSpecWithBases(&@_Type_spec, bases.get())", type.TypeName());
            }
            else
            {
                w.write("PyType_FromSpec(&@_Type_spec)", type.TypeName());
            }
            w.write(" };\n");

            auto format = R"(if (!type_object)
{
    return -1;
}
if (PyModule_AddObject(module, "@", type_object.get()) != 0)
{
    return -1;
}
py::winrt_type<%>::python_type = reinterpret_cast<PyTypeObject*>(type_object.detach());)";
            w.write(format,
                type.TypeName(),
                winrt_type_param);
        }

        w.write("\n}");
    }

    void write_namespace_module_exec_func(writer& w, cache::namespace_members const& members)
    {
        w.write(R"(
static int module_exec(PyObject* module)
{
    py::pyobj_handle bases { PyTuple_Pack(1, py::winrt_type<py::winrt_base>::python_type) };
)");

        {
            writer::indent_guard g{ w };

            settings.filter.bind_each<write_namespace_module_exec_init_python_type>(members.classes)(w);
            settings.filter.bind_each<write_namespace_module_exec_init_python_type>(members.interfaces)(w);
            settings.filter.bind_each<write_namespace_module_exec_init_python_type>(members.structs)(w);

            w.write("\nreturn 0;\n");
        }

        w.write("}\n");
    }

    void write_namespace_module_def(writer& w, std::string_view const& ns)
    {
        auto format = R"(
static PyModuleDef_Slot module_slots[] = {
    {Py_mod_exec, module_exec},
    {0, nullptr}
};

PyDoc_STRVAR(module_doc, "%");

static PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "%",
    module_doc,
    0,
    nullptr,
    module_slots,
    nullptr,
    nullptr,
    nullptr
};

PyMODINIT_FUNC
PyInit_%(void)
{
    return PyModuleDef_Init(&module_def);
}
)";
        w.write(format, ns, bind<write_module_name>(ns), bind<write_module_name>(ns));
    }

    void write_namespace_initialization(writer& w, std::string_view const& ns, cache::namespace_members const& members)
    {
        w.write("\n// ----- % Initialization --------------------\n", ns);

        write_namespace_module_exec_func(w, members);
        write_namespace_module_def(w, ns);
    }
}