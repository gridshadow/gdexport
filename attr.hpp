#ifndef GDEXPORT_ATTR_HPP
#define GDEXPORT_ATTR_HPP

#ifdef GDEXPORT_GENERATING
#define GDATTR(x) [[godot_##x]]
#else
#define GDATTR(x)
#endif

#endif // AA-LOW-GDEXPORT_ATTR_HPP

