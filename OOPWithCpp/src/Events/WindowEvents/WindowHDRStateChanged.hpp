//
// Created by forwardfax3 on 10/11/2026.
//
#pragma once
#include "BaseEvent.hpp"


namespace OWC
{
    class WindowHDRStateChanged : public BaseEvent
    {
    public:
        WindowHDRStateChanged() : BaseEvent(EventType::WindowHDRStateChange) {}
        ~WindowHDRStateChanged() override = default;
        WindowHDRStateChanged(const WindowHDRStateChanged&) = default;
        WindowHDRStateChanged& operator=(const WindowHDRStateChanged&) = default;
        WindowHDRStateChanged(WindowHDRStateChanged&&) = default;
        WindowHDRStateChanged& operator=(WindowHDRStateChanged&&) = default;

        static consteval EventType GetStaticType() { return EventType::WindowHDRStateChange; }
    };
}

