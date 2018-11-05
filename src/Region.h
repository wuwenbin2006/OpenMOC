/**
 * @file Region.h
 * @brief The Region class.
 * @date March 10, 2017
 * @author William Boyd, MIT, Course 22 (wboyd@mit.edu)
 */
 #ifndef REGION_H_
#define REGION_H_
 #ifdef __cplusplus
#ifdef SWIG
#include "Python.h"
#endif
#include "Surface.h"
#include "boundary_type.h"
#include <limits>
#endif

 /* Forward declarations to resolve circular dependencies */
class Intersection;
class Union;
class Complement;
class Halfspace;

/**
 * @enum regionType
 * @brief The types of regions supported by OpenMOC.
 */
enum regionType {
  /** The intersection of one or more regions */
  INTERSECTION,
   /** The union of one or more regions */
  UNION,
   /** The complement of a region */
  COMPLEMENT,
   /** The side of a surface */
  HALFSPACE
};

/**
 * @class Region Region.h "src/Region.h"
 * @brief A region of space that can be assigned to a Cell.
 */
class Region {

protected:

   /** The type of Region (ie, UNION, INTERSECTION, etc) */
  regionType _region_type;

   /** A collection of the nodes within the Region */
  std::vector<Region*> _nodes;

  /** The parent region, a region which has this region among its nodes */
  Region* _parent_region;

public:
  Region();
  virtual ~Region();

  /* Functions for constructing the Region / other Regions */
  virtual void addNode(Region* node, bool clone=true);
  virtual void addNodes(std::vector<Region*> nodes, bool clone=true);
  void removeHalfspace(Surface* surface, int halfspace);
  regionType getRegionType();
  void setParentRegion(Region* node);
  Region* getParentRegion();

  /* Getter functions */
  virtual std::vector<Region*> getNodes();
  virtual std::vector<Region*> getAllNodes();
  virtual std::map<int, Halfspace*> getAllSurfaces();

  /* Worker functions */
  virtual double getMinX();
  virtual double getMaxX();
  virtual double getMinY();
  virtual double getMaxY();
  virtual double getMinZ();
  virtual double getMaxZ();
  virtual boundaryType getMinXBoundaryType();
  virtual boundaryType getMaxXBoundaryType();
  virtual boundaryType getMinYBoundaryType();
  virtual boundaryType getMaxYBoundaryType();
  virtual boundaryType getMinZBoundaryType();
  virtual boundaryType getMaxZBoundaryType();
  virtual bool containsPoint(Point* point) =0;
  virtual double minSurfaceDist(Point* point, double azim, double polar=M_PI_2);
  virtual double minSurfaceDist(LocalCoords* coords);
  virtual Region* clone();
  virtual std::string toString() = 0;
};

/**
 * @class Intersection Region.h "src/Region.h"
 * @brief An intersection of two or more Regions.
 */
class Intersection : public Region {

public:
  Intersection(std::vector<Region*> nodes = std::vector<Region*>());
  bool containsPoint(Point* point);
  std::string toString();
};

/**
 * @class Union Region.h "src/Region.h"
 * @brief A union of two or more Regions.
 */
class Union : public Region {

public:
  Union(std::vector<Region*> nodes = std::vector<Region*>());
  bool containsPoint(Point* point);
  std::string toString();
};

/**
 * @class Complement Region.h "src/Region.h"
 * @brief A complement of a Region.
 */
class Complement : public Region {
protected:
  /* Complement could always be quivalent to a Union, an Intersection, or the 
     opposite of a Complement or a Halfspace */
  Region* _equivalent;
  
public:
  Complement(Region* node = NULL);
  ~Complement();
  void addNode(Region* node, bool clone=true);
  bool containsPoint(Point* point);
  Region* getEquivalent();
  std::string toString();
};

/**
 * @class Halfspace Region.h "src/Region.h"
 * @brief A positive or negative halfspace Region.
 */
class Halfspace : public Region {

public:

  /** A pointer to the Surface object */
  Surface* _surface;

  /** The halfspace associated with this surface */
  int _halfspace;

  Halfspace(int halfspace, Surface* surface);
  Halfspace* clone();
  Surface* getSurface();
  int getHalfspace();
  void reverseHalfspace();
  std::map<int, Halfspace*> getAllSurfaces();
  double getMinX();
  double getMaxX();
  double getMinY();
  double getMaxY();
  double getMinZ();
  double getMaxZ();
  boundaryType getMinXBoundaryType();
  boundaryType getMaxXBoundaryType();
  boundaryType getMinYBoundaryType();
  boundaryType getMaxYBoundaryType();
  boundaryType getMinZBoundaryType();
  boundaryType getMaxZBoundaryType();
  bool containsPoint(Point* point);
  double minSurfaceDist(Point* point, double azim, double polar=M_PI_2);
  double minSurfaceDist(LocalCoords* coords);
  std::string toString();
};

/**
 * @class RectangularPrism Region.h "src/Region.h"
 * @brief An infinite rectangular prism aligned with the z-axis.
 */
class RectangularPrism : public Intersection {

public:
  RectangularPrism(double width_x, double width_y, double origin_x=0.,
                   double origin_y=0., double width_z=1E100,
                   double origin_z=0.);
  void setBoundaryType(boundaryType boundary_type);
};

#endif /* REGION_H_ */
