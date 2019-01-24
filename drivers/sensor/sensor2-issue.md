## Introduction

The existing sensor API does not seem to suit real-world sensor needs,
presents data in an inconvenient way, and is not suited for low-power
applications.  This RFC details the limitation of the API and proposes a
new API better suited to embedded sensor applications.

### Problem description

[//]: # (Why do we want this change and what problem are we trying to address?)

### Proposed change

[//]: # (A brief summary of the proposed change - the 10,000 ft view on what it will change once this change is implemented.)

* Sensor observations should be presented as device-specific structures
  that capture all measurements in a space-efficient representation.
* Sensor configuration should be primarily defined at build time, with
  device-specific API for any configuration that may be runtime
  configurable.
* Control for sensor drivers (e.g. thread support) will not be part of
  the driver.


## Detailed RFC

[//]: # (In this section of the document the target audience is the dev team. Upon reading this section each engineer should have a rather clear picture of what needs to be done in order to implement the described feature.)

### Limitations of Existing Solution

The primary limitation is the assumption that a generic API to sensors
has real-world value.  In the context of Linux which provides an
Industrial IO API that can be shared by various applications this may be
useful.  In the context of an embedded device running task-specific
fixed firmware on fixed hardware abstraction has limited benefit, and
requires significant complexity and overhead.

#### Representation

Values are represented in a two element form, providing the integer
value and the numerator of a fraction with denominator 10^6.  Both
elements are signed, and the combined values require 64 bits of storage.
No magnitude constraints are imposed, resulting in multiple
representations of a value such as 0.5, some of which may have negative
integer or fraction components.

In the real world many sensor values can be represented as 16-bit
values, and few would require 64 bits.

#### Measurements ("Channels")

The nomenclature "channel" is poorly chosen.  What sensors provide is a
measurement of a physical attribute.

The API defines a fixed set of expected measurements, each with a
pre-assigned unit which may not be suitable to the sensor.

There is no provision for supporting a new measurement without changing
the overall device API.

The retrieval API supports only a signal value associated with each
measurement, mandating three invocations to obtain all three axes for
vector measurements like acceleration.  There is no clear provision to
ensure retrieved values correspond to the same observation.

#### Triggers

There is a complex and hard-coded set of expected triggers.  None are
relevant for all sensors; several are relevant only to specific classes
of sensors (e.g. accelerometers).

The "trigger" concept is also mixed with the sensor basic operating
mode, which is basically either on-demand (something must request an
observation) or periodic (an observation is produced at a fixed rate
without a specific request).  When a sensor also supports
observation-based alerting like threshold transitions proper
configuration becomes complex.

Finally, the trigger concept is also mixed with the model used to
process driver operations.  Most sensor Kconfig files define a
`TRIGGER_MODE` which is either none (the application must invoke driver
functions to make progress), or a global or owned thread.  Each sensor
configures its own thread priority and stack size.  The Kconfig
specification and driver support infrastructure is replicated in every
driver.

#### Attributes

This is another hard-coded set of characteristics designed to provide a
"generic" api to the union of all conceivable sensor configuration and
state properties, again based on an assumption that the 64-bit
two-component scalar value is a sufficient and acceptable way to access
these properties.

### Proposed change (Detailed)

#### Representation

Currently the sensor API hard-codes the set of measurements, requires
that they be in particular units (some of which are non-standard), and
represents values in a 64-bit two-element form that is underdefined and
requires a complex tests to determine the sign of the value and floating
point or 64-bit arithmetic to scale values.

[//]: # (This section is freeform - you should describe your change in as much detail as possible. Please also ensure to include any context or background info here.  For example, do we have existing components which can be reused or altered.)

[//]: # (By reading this section, each team member should be able to know what exactly you're planning to change and how.)

### Dependencies

[//]: # (Highlight how the change may affect the rest of the project (new components, modifications in other areas), or other teams/projects.)

### Concerns and Unresolved Questions

[//]: # (List any concerns, unknowns, and generally unresolved questions etc.)

## Alternatives

[//]: # (List any alternatives considered, and the reasons for choosing this option over them.)
