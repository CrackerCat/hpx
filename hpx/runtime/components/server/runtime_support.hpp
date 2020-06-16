//  Copyright (c) 2007-2018 Hartmut Kaiser
//  Copyright (c) 2011 Bryce Lelbach
//  Copyright (c) 2011-2017 Thomas Heller
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>
#include <hpx/assert.hpp>
#include <hpx/modules/errors.hpp>
#include <hpx/synchronization/condition_variable.hpp>
#include <hpx/synchronization/mutex.hpp>
#include <hpx/synchronization/spinlock.hpp>
#include <hpx/performance_counters/counters.hpp>
#include <hpx/modules/plugin.hpp>
#include <hpx/plugins/plugin_factory_base.hpp>
#include <hpx/modules/program_options.hpp>
#include <hpx/runtime/actions/component_action.hpp>
#include <hpx/runtime/actions/manage_object_action.hpp>
#include <hpx/runtime/components/component_type.hpp>
#include <hpx/runtime/components/server/create_component.hpp>
#include <hpx/runtime_configuration/static_factory_data.hpp>
#include <hpx/runtime/find_here.hpp>
#include <hpx/runtime/parcelset/locality.hpp>
#include <hpx/traits/action_does_termination_detection.hpp>
#include <hpx/traits/is_component.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <hpx/config/warnings_prefix.hpp>

namespace hpx { namespace components { namespace server
{
    ///////////////////////////////////////////////////////////////////////////
    class runtime_support
    {
    private:
        typedef lcos::local::spinlock plugin_map_mutex_type;

        struct plugin_factory
        {
            plugin_factory(
                  std::shared_ptr<plugins::plugin_factory_base> const& f,
                  hpx::util::plugin::dll const& d, bool enabled)
              : first(f), second(d), isenabled(enabled)
            {}

            std::shared_ptr<plugins::plugin_factory_base> first;
            hpx::util::plugin::dll const& second;
            bool isenabled;
        };
        typedef plugin_factory plugin_factory_type;
        typedef std::map<std::string, plugin_factory_type> plugin_map_type;

        typedef std::map<std::string, hpx::util::plugin::dll> modules_map_type;
        typedef std::vector<static_factory_load_data_type> static_modules_type;

    public:
        typedef runtime_support type_holder;

        static component_type get_component_type()
        {
            return components::get_component_type<runtime_support>();
        }
        static void set_component_type(component_type t)
        {
            components::set_component_type<runtime_support>(t);
        }

        // constructor
        explicit runtime_support(hpx::util::runtime_configuration & cfg);

        ~runtime_support()
        {
            tidy();
        }

        /// \brief finalize() will be called just before the instance gets
        ///        destructed
        ///
        /// \param self [in] The HPX \a thread used to execute this function.
        /// \param appl [in] The applier to be used for finalization of the
        ///             component instance.
        static constexpr void finalize() {}

        void delete_function_lists();
        void tidy();

        // This component type requires valid locality id for its actions to
        // be invoked
        static bool is_target_valid(naming::id_type const& id)
        {
            return naming::is_locality(id);
        }

        ///////////////////////////////////////////////////////////////////////
        // exposed functionality of this component

        /// \brief Actions to create new objects
        template <typename Component>
        naming::gid_type create_component();

        template <typename Component, typename T, typename ...Ts>
        naming::gid_type create_component(T v, Ts... vs);

        template <typename Component>
        std::vector<naming::gid_type> bulk_create_component(std::size_t count);

        template <typename Component, typename T, typename ...Ts>
        std::vector<naming::gid_type> bulk_create_component(
            std::size_t count, T v, Ts... vs);

        template <typename Component>
        naming::gid_type copy_create_component(
            std::shared_ptr<Component> const& p, bool);

        template <typename Component>
        naming::gid_type migrate_component_to_here(
            std::shared_ptr<Component> const& p, naming::id_type);

        /// \brief Gracefully shutdown this runtime system instance
        void shutdown(double timeout, naming::id_type const& respond_to);

        /// \brief Gracefully shutdown runtime system instances on all localities
        void shutdown_all(double timeout);

        /// \brief Shutdown this runtime system instance
        HPX_NORETURN void terminate(
            naming::id_type const& respond_to);

        void terminate_act(naming::id_type const& id) { terminate(id); }

        /// \brief Shutdown runtime system instances on all localities
        HPX_NORETURN void terminate_all();

        void terminate_all_act() { terminate_all(); }

        /// \brief Retrieve configuration information
        util::section get_config();

        /// \brief Load all components on this locality.
        int load_components();

        void call_startup_functions(bool pre_startup);
        void call_shutdown_functions(bool pre_shutdown);

        /// \brief Force a garbage collection operation in the AGAS layer.
        void garbage_collect();

        /// \brief Create the given performance counter instance.
        naming::gid_type create_performance_counter(
            performance_counters::counter_info const& info);

        /// \brief Remove the given locality from our connection cache
        void remove_from_connection_cache(naming::gid_type const& gid,
            parcelset::endpoints_type const& eps);

        /// \brief termination detection
#if defined(HPX_HAVE_NETWORKING)
        void dijkstra_termination(std::uint32_t initiating_locality_id,
            std::uint32_t num_localities, bool dijkstra_token);
#endif

        ///////////////////////////////////////////////////////////////////////
        // Each of the exposed functions needs to be encapsulated into a action
        // type, allowing to generate all require boilerplate code for threads,
        // serialization, etc.
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, load_components);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, call_startup_functions);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, call_shutdown_functions);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, shutdown);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, shutdown_all);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, terminate_act,
            terminate_action);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, terminate_all_act,
            terminate_all_action);

        // even if this is not a short/minimal action, we still execute it
        // directly to avoid a deadlock condition inside the thread manager
        // waiting for this thread to finish, which waits for the thread
        // manager to exit
        HPX_DEFINE_COMPONENT_DIRECT_ACTION(runtime_support, get_config);

        HPX_DEFINE_COMPONENT_ACTION(runtime_support, garbage_collect);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, create_performance_counter);
        HPX_DEFINE_COMPONENT_ACTION(runtime_support, remove_from_connection_cache);

        HPX_DEFINE_COMPONENT_ACTION(runtime_support, dijkstra_termination);

        ///////////////////////////////////////////////////////////////////////
        /// \brief Start the runtime_support component
        void run();

        /// \brief Wait for the runtime_support component to notify the calling
        ///        thread.
        ///
        /// This function will be called from the main thread, causing it to
        /// block while the HPX functionality is executed. The main thread will
        /// block until the shutdown_action is executed, which in turn notifies
        /// all waiting threads.
        void wait();

        /// \brief Notify all waiting (blocking) threads allowing the system to
        ///        be properly stopped.
        ///
        /// \note      This function can be called from any thread.
        void stop(double timeout, naming::id_type const& respond_to,
            bool remove_from_remote_caches);

        /// called locally only
        void stopped();
        void notify_waiting_main();

        bool was_stopped() const { return stop_called_; }

        void add_pre_startup_function(startup_function_type f);
        void add_startup_function(startup_function_type f);
        void add_pre_shutdown_function(shutdown_function_type f);
        void add_shutdown_function(shutdown_function_type f);

        void remove_here_from_connection_cache();
        void remove_here_from_console_connection_cache();

#if defined(HPX_HAVE_NETWORKING)
        ///////////////////////////////////////////////////////////////////////
        void register_message_handler(char const* message_handler_type,
            char const* action, error_code& ec);

        parcelset::policies::message_handler* create_message_handler(
            char const* message_handler_type, char const* action,
            parcelset::parcelport* pp, std::size_t num_messages,
            std::size_t interval, error_code& ec);
        serialization::binary_filter* create_binary_filter(
            char const* binary_filter_type, bool compress,
            serialization::binary_filter* next_filter, error_code& ec);

        // notify of message being sent
        void dijkstra_make_black();
#endif

    protected:
        // Load all components from the ini files found in the configuration
        int load_components(util::section& ini, naming::gid_type const& prefix,
            naming::resolver_client& agas_client,
            hpx::program_options::options_description& options,
            std::set<std::string>& startup_handled);

#if !defined(HPX_HAVE_STATIC_LINKING)
        bool load_component(hpx::util::plugin::dll& d,
            util::section& ini, std::string const& instance,
            std::string const& component, filesystem::path const& lib,
            naming::gid_type const& prefix, naming::resolver_client& agas_client,
            bool isdefault, bool isenabled,
            hpx::program_options::options_description& options,
            std::set<std::string>& startup_handled);
        bool load_component_dynamic(
            util::section& ini, std::string const& instance,
            std::string const& component, filesystem::path lib,
            naming::gid_type const& prefix, naming::resolver_client& agas_client,
            bool isdefault, bool isenabled,
            hpx::program_options::options_description& options,
            std::set<std::string>& startup_handled);

        bool load_startup_shutdown_functions(hpx::util::plugin::dll& d,
            error_code& ec);
        bool load_commandline_options(hpx::util::plugin::dll& d,
            hpx::program_options::options_description& options,
            error_code& ec);
#endif

        bool load_component_static(
            util::section& ini, std::string const& instance,
            std::string const& component, filesystem::path const& lib,
            naming::gid_type const& prefix, naming::resolver_client& agas_client,
            bool isdefault, bool isenabled,
            hpx::program_options::options_description& options,
            std::set<std::string>& startup_handled);
        bool load_startup_shutdown_functions_static(std::string const& module,
            error_code& ec);
        bool load_commandline_options_static(
            std::string const& module,
            hpx::program_options::options_description& options,
            error_code& ec);

        // Load all plugins from the ini files found in the configuration
        bool load_plugins(util::section& ini,
            hpx::program_options::options_description& options,
            std::set<std::string>& startup_handled);

#if !defined(HPX_HAVE_STATIC_LINKING)
        bool load_plugin(hpx::util::plugin::dll& d,
            util::section& ini, std::string const& instance,
            std::string const& component, filesystem::path const& lib,
            bool isenabled,
            hpx::program_options::options_description& options,
            std::set<std::string>& startup_handled);
        bool load_plugin_dynamic(
            util::section& ini, std::string const& instance,
            std::string const& component, filesystem::path lib,
            bool isenabled,
            hpx::program_options::options_description& options,
            std::set<std::string>& startup_handled);
#endif

        // the name says it all
        std::size_t dijkstra_termination_detection(
            std::vector<naming::id_type> const& locality_ids);

#if defined(HPX_HAVE_NETWORKING)
        void send_dijkstra_termination_token(
            std::uint32_t target_locality_id,
            std::uint32_t initiating_locality_id,
            std::uint32_t num_localities, bool dijkstra_token);
#endif

    private:
        std::mutex mtx_;
        std::condition_variable wait_condition_;
        std::condition_variable stop_condition_;
        bool stop_called_;
        bool stop_done_;
        bool terminated_;
        std::thread::id main_thread_id_;
        std::atomic<bool> shutdown_all_invoked_;

#if defined(HPX_HAVE_NETWORKING)
        typedef hpx::lcos::local::spinlock dijkstra_mtx_type;
        dijkstra_mtx_type dijkstra_mtx_;
        lcos::local::condition_variable_any dijkstra_cond_;
        bool dijkstra_color_;   // false: white, true: black
#endif

        plugin_map_mutex_type p_mtx_;

        plugin_map_type plugins_;
        modules_map_type & modules_;
        static_modules_type static_modules_;

        lcos::local::spinlock globals_mtx_;
        std::list<startup_function_type> pre_startup_functions_;
        std::list<startup_function_type> startup_functions_;
        std::list<shutdown_function_type> pre_shutdown_functions_;
        std::list<shutdown_function_type> shutdown_functions_;
    };

    ///////////////////////////////////////////////////////////////////////////
    // Functions wrapped by create_component actions below
#if defined(__NVCC__)
    template <typename Component>
    naming::gid_type runtime_support::create_component()
    {
        HPX_ASSERT(false);
        return naming::gid_type();
    }

    template <typename Component, typename T, typename ...Ts>
    naming::gid_type runtime_support::create_component(T v, Ts... vs)
    {
        HPX_ASSERT(false);
        return naming::gid_type();
    }
#else
    template <typename Component>
    naming::gid_type runtime_support::create_component()
    {
        components::component_type const type =
            components::get_component_type<
                typename Component::wrapped_type>();

        typedef typename Component::wrapping_type wrapping_type;
        naming::gid_type id = create<wrapping_type>();
        LRT_(info) << "successfully created component " << id
            << " of type: " << components::get_component_type_name(type);

        return id;
    }

    template <typename Component, typename T, typename ...Ts>
    naming::gid_type runtime_support::create_component(T v, Ts... vs)
    {
        components::component_type const type =
            components::get_component_type<
                typename Component::wrapped_type>();

        typedef typename Component::wrapping_type wrapping_type;
        // Note, T and Ts can't be (non-const) references, and parameters
        // should be moved to allow for move-only constructor argument
        // types.
        naming::gid_type id = create<wrapping_type>(std::move(v), std::move(vs)...);

        LRT_(info) << "successfully created component " << id
        << " of type: " << components::get_component_type_name(type);

        return id;
    }
#endif

    template <typename Component>
    std::vector<naming::gid_type>
    runtime_support::bulk_create_component(std::size_t count)
    {
        components::component_type const type =
            components::get_component_type<
                typename Component::wrapped_type>();


        std::vector<naming::gid_type> ids;
        ids.reserve(count);

        typedef typename Component::wrapping_type wrapping_type;
        for (std::size_t i = 0; i != count; ++i)
        {
            ids.push_back(create<wrapping_type>());
        }

        LRT_(info) << "successfully created " << count //-V128
                   << " component(s) of type: "
                   << components::get_component_type_name(type);

        return ids;
    }

    template <typename Component, typename T, typename ...Ts>
    std::vector<naming::gid_type>
    runtime_support::bulk_create_component(std::size_t count, T v, Ts ... vs)
    {
        components::component_type const type =
            components::get_component_type<
                typename Component::wrapped_type>();

        std::vector<naming::gid_type> ids;
        ids.reserve(count);

        typedef typename Component::wrapping_type wrapping_type;
        for (std::size_t i = 0; i != count; ++i)
        {
            ids.push_back(create<wrapping_type>(v, vs...));
        }

        LRT_(info) << "successfully created " << count //-V128
                   << " component(s) of type: "
                   << components::get_component_type_name(type);

        return ids;
    }

    template <typename Component>
    naming::gid_type runtime_support::copy_create_component(
        std::shared_ptr<Component> const& p, bool local_op)
    {
        components::component_type const type =
            components::get_component_type<
                typename Component::wrapped_type>();

        typedef typename Component::wrapping_type wrapping_type;
        naming::gid_type id;

        if (!local_op) {
            id = create<wrapping_type>(std::move(*p));
        }
        else {
            id = create<wrapping_type>(*p);
        }

        LRT_(info) << "successfully created component " << id
            << " of type: " << components::get_component_type_name(type);

        return id;
    }

    ///////////////////////////////////////////////////////////////////////////
    template <typename Component>
    naming::gid_type runtime_support::migrate_component_to_here(
        std::shared_ptr<Component> const& p, naming::id_type to_migrate)
    {
        components::component_type const type =
            components::get_component_type<
                typename Component::wrapped_type>();

        // create a local instance by copying the bits and remapping the id in
        // AGAS
        naming::gid_type migrated_id = to_migrate.get_gid();

        typedef typename Component::wrapping_type wrapping_type;
        typename wrapping_type::derived_type* new_instance = nullptr;

        naming::gid_type id = create_migrated<wrapping_type>(migrated_id,
            reinterpret_cast<void**>(&new_instance), std::move(*p));

        // sanity checks
        if (!id || new_instance == nullptr)
        {
            // we should not get here (id should not be invalid)
            HPX_THROW_EXCEPTION(hpx::invalid_status,
                "runtime_support::migrate_component_to_here",
                "could not create copy of given component");
            return naming::invalid_gid;
        }
        if (id != migrated_id)
        {
            // we should not get here either (the ids should be the same)
            HPX_THROW_EXCEPTION(hpx::invalid_status,
                "runtime_support::migrate_component_to_here",
                "could not create copy of given component (the new id is "
                    "different from the original id)");
            return naming::invalid_gid;
        }

        LRT_(info) << "successfully migrated component " << id
            << " of type: " << components::get_component_type_name(type)
            << " to locality: " << find_here();

        // inform the newly created component that it has been migrated
        new_instance->on_migrated();

        // At this point the object has been fully migrated. We now remove
        // the object from the AGAS table of migrated objects. This is
        // necessary as this object might have been migrated off this locality
        // before it was migrated back.
        agas::unmark_as_migrated(id);

        to_migrate.make_unmanaged();

        return id;
    }
}}}

#include <hpx/config/warnings_suffix.hpp>

HPX_ACTION_USES_LARGE_STACK(
    hpx::components::server::runtime_support::load_components_action)
HPX_ACTION_USES_MEDIUM_STACK(
    hpx::components::server::runtime_support::call_startup_functions_action)
HPX_ACTION_USES_MEDIUM_STACK(
    hpx::components::server::runtime_support::call_shutdown_functions_action)
HPX_ACTION_USES_MEDIUM_STACK(
    hpx::components::server::runtime_support::shutdown_action)
HPX_ACTION_USES_MEDIUM_STACK(
    hpx::components::server::runtime_support::shutdown_all_action)
HPX_ACTION_USES_MEDIUM_STACK(
    hpx::components::server::runtime_support::create_performance_counter_action)
HPX_ACTION_USES_MEDIUM_STACK(
    hpx::components::server::runtime_support::dijkstra_termination_action)

HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::load_components_action,
    load_components_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::call_startup_functions_action,
    call_startup_functions_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::call_shutdown_functions_action,
    call_shutdown_functions_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::shutdown_action,
    shutdown_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::shutdown_all_action,
    shutdown_all_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::terminate_action,
    terminate_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::terminate_all_action,
    terminate_all_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::get_config_action,
    get_config_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::garbage_collect_action,
    garbage_collect_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::create_performance_counter_action,
    create_performance_counter_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::remove_from_connection_cache_action,
    remove_from_connection_cache_action)
HPX_REGISTER_ACTION_DECLARATION(
    hpx::components::server::runtime_support::dijkstra_termination_action,
    dijkstra_termination_action)

namespace hpx { namespace components { namespace server
{
    template <typename Component, typename ...Ts>
    struct create_component_action
      : ::hpx::actions::action<
            naming::gid_type (runtime_support::*)(Ts...)
          , &runtime_support::create_component<Component, Ts...>
          , create_component_action<Component, Ts...> >
    {};

    template <typename Component>
    struct create_component_action<Component>
      : ::hpx::actions::action<
            naming::gid_type (runtime_support::*)()
          , &runtime_support::create_component<Component>
          , create_component_action<Component> >
    {};

    template <typename Component, typename ...Ts>
    struct create_component_direct_action
      : ::hpx::actions::direct_action<
            naming::gid_type (runtime_support::*)(Ts...)
          , &runtime_support::create_component<Component, Ts...>
          , create_component_direct_action<Component, Ts...> >
    {};

    template <typename Component>
    struct create_component_direct_action<Component>
      : ::hpx::actions::direct_action<
            naming::gid_type (runtime_support::*)()
          , &runtime_support::create_component<Component>
          , create_component_direct_action<Component> >
    {};

    ///////////////////////////////////////////////////////////////////////////
    template <typename Component, typename ...Ts>
    struct bulk_create_component_action
      : ::hpx::actions::action<
            std::vector<naming::gid_type> (runtime_support::*)(std::size_t, Ts...)
          , &runtime_support::bulk_create_component<Component, Ts...>
          , bulk_create_component_action<Component, Ts...> >
    {};

    template <typename Component>
    struct bulk_create_component_action<Component>
      : ::hpx::actions::action<
            std::vector<naming::gid_type> (runtime_support::*)(std::size_t)
          , &runtime_support::bulk_create_component<Component>
          , bulk_create_component_action<Component> >
    {};

    template <typename Component, typename ...Ts>
    struct bulk_create_component_direct_action
      : ::hpx::actions::direct_action<
            std::vector<naming::gid_type> (runtime_support::*)(std::size_t, Ts...)
          , &runtime_support::bulk_create_component<Component, Ts...>
          , bulk_create_component_direct_action<Component, Ts...> >
    {};

    template <typename Component>
    struct bulk_create_component_direct_action<Component>
      : ::hpx::actions::direct_action<
            std::vector<naming::gid_type> (runtime_support::*)(std::size_t)
          , &runtime_support::bulk_create_component<Component>
          , bulk_create_component_direct_action<Component> >
    {};

    ///////////////////////////////////////////////////////////////////////////
    template <typename Component>
    struct copy_create_component_action
      : ::hpx::actions::action<
            naming::gid_type (runtime_support::*)(
                std::shared_ptr<Component> const&, bool)
          , &runtime_support::copy_create_component<Component>
          , copy_create_component_action<Component> >
    {};
    template <typename Component>
    struct migrate_component_here_action
      : ::hpx::actions::action<
            naming::gid_type (runtime_support::*)(
                std::shared_ptr<Component> const&, naming::id_type)
          , &runtime_support::migrate_component_to_here<Component>
          , migrate_component_here_action<Component> >
    {};
}}}

namespace hpx { namespace traits
{
    ///////////////////////////////////////////////////////////////////////////
    // Termination detection does not make this locality black
#if !defined(HPX_COMPUTE_DEVICE_CODE)
    template <>
    struct action_does_termination_detection<
        hpx::components::server::runtime_support::dijkstra_termination_action>
    {
        static bool call()
        {
            return true;
        }
    };
#endif

    // runtime_support is a (hand-rolled) component
    template <>
    struct is_component<components::server::runtime_support>
      : std::true_type
    {};
}}

