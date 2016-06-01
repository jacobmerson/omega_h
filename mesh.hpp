class Dist;
namespace inertia {
struct Rib;
}

enum Partition {
  ELEMENT_BASED,
  GHOSTED,
  VERTEX_BASED
};

class Mesh {
  public:
    Mesh();
    void set_comm(CommPtr comm);
    void set_dim(Int dim);
    void set_verts(LO nverts);
    void set_ents(Int dim, Adj down);
    CommPtr comm() const;
    Partition partition();
    Int dim() const;
    LO nents(Int dim) const;
    LO nelems() const;
    LO nverts() const;
    template <typename T>
    void add_tag(Int dim, std::string const& name, Int ncomps);
    template <typename T>
    void add_tag(Int dim, std::string const& name, Int ncomps,
        Read<T> array);
    template <typename T>
    void set_tag(Int dim, std::string const& name, Read<T> array);
    template <typename T>
    Tag<T> const& get_tag(Int dim, std::string const& name) const;
    template <typename T>
    Read<T> get_array(Int dim, std::string const& name) const;
    void remove_tag(Int dim, std::string const& name);
    bool has_tag(Int dim, std::string const& name) const;
    Int ntags(Int dim) const;
    TagBase const* get_tag(Int dim, Int i) const;
    bool has_ents(Int dim) const;
    bool has_adj(Int from, Int to) const;
    Adj get_adj(Int from, Int to) const;
    Adj ask_down(Int from, Int to);
    LOs ask_verts_of(Int dim);
    Adj ask_up(Int from, Int to);
    Graph ask_star(Int dim);
    Graph ask_dual();
  private:
    typedef std::shared_ptr<TagBase> TagPtr;
    typedef std::shared_ptr<Adj> AdjPtr;
    typedef std::shared_ptr<Dist> DistPtr;
    typedef std::shared_ptr<inertia::Rib> RibPtr;
    typedef std::vector<TagPtr> TagVector;
    typedef TagVector::iterator TagIter;
    typedef TagVector::const_iterator TagCIter;
    TagIter tag_iter(Int dim, std::string const& name);
    TagCIter tag_iter(Int dim, std::string const& name) const;
    void check_dim(Int dim) const;
    void check_dim2(Int dim) const;
    void add_adj(Int from, Int to, Adj adj);
    Adj derive_adj(Int from, Int to);
    Adj ask_adj(Int from, Int to);
    void react_to_set_tag(Int dim, std::string const& name);
    CommPtr comm_;
    Partition partition_;
    Int dim_;
    LO nents_[DIMS];
    TagVector tags_[DIMS];
    AdjPtr adjs_[DIMS][DIMS];
    Remotes owners_[DIMS];
    DistPtr dists_[DIMS];
    RibPtr rib_hints_;
  public:
    void add_coords(Reals array);
    Reals coords() const;
    void set_coords(Reals array);
    Read<GO> ask_globals(Int dim);
    void forget_globals();
    Reals ask_edge_lengths();
    Reals ask_qualities();
    void set_owners(Int dim, Remotes owners);
    Remotes ask_owners(Int dim);
    Read<I8> owned(Int dim);
    Dist ask_dist(Int dim);
    void set_partition(Partition partition);
    void migrate(Remotes new_elems2old_owners);
    void reorder();
    void balance();
};
