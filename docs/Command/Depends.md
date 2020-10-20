[[/Command/Depends]] -- Output model dependency tree

# Synopsis

GLM:

~~~
#option depends
~~~

Shell:

~~~
bash$ gridlabd [...] filename.glm --depends [...]
~~~

# Description

The `--depends` command line option causes GridLAB-D to output a dependency tree for the model in its current state.  The position of the option on the command line determines which files are considered when processing the dependency tree.

# Example

Consider the following GLM file name `example.glm`:

~~~
#include "local-file.glm"
#include <system-file.glm>
#include [http://server/remote-file.glm]
~~~

The following is an example of using the `--depends` option:

~~~
bash$ gridlabd example.glm --depends
# generated by gridlabd 4.2.1-200214-develop_add_depends_option

all: example.glm

example.glm: local-file.glm /usr/local/share/gridlabd/system-file.glm remote-file.glm

remote-file.glm: http://server/remote-file.glm

~~~