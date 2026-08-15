#pragma once
#include "common.h"

namespace strikers {

template <typename _t>
class flatgraph final
{
public:
	enum class traverse_mode
	{
		width,
		depth
	};

	using node_idx = uint32;
	static constexpr node_idx k_invalid = 0u;
	static constexpr node_idx k_root = 1u;

	flatgraph()
	{
		m_nodes.reserve(256u);
		node_idx invalid = allocate_node(); // k_invalid
		node_idx root = allocate_node(); // k_root
	}

	bool is_valid(node_idx node) const
	{
		return m_nodes[node].m_is_valid;
	}
	node_idx parent(node_idx node) const
	{
		return m_nodes[node].m_parent;
	}
	node_idx first_child(node_idx node) const
	{
		return m_nodes[node].m_first_child;
	}
	node_idx next_sib(node_idx node) const
	{
		return m_nodes[node].m_next_sibling;
	}
	node_idx prev_sib(node_idx node) const
	{
		return m_nodes[node].m_prev_sibling;
	}
	node_idx last_child(node_idx node) const
	{
		return prev_sib(m_nodes[node].m_first_child);
	}
	bool has_children(node_idx node) const
	{
		return is_valid(first_child(node));
	}

	class node final
	{
	public:
		node& parent()
		{
			return m_graph->m_nodes[m_graph->parent(m_idx)];
		}
		node& first_child()
		{
			return m_graph->m_nodes[m_graph->first_child(m_idx)];
		}
		node& next_sib()
		{
			return m_graph->m_nodes[m_graph->next_sib(m_idx)];
		}
		node& prev_sib()
		{
			return m_graph->m_nodes[m_graph->prev_sib(m_idx)];
		}
		node& last_child()
		{
			return m_graph->m_nodes[m_graph->last_child(m_idx)];
		}
		bool has_children() const
		{
			return m_graph->has_children(m_idx);
		}
		node_idx get_index() const
		{
			return m_idx;
		}
		const _t& data() const
		{
			return m_data;
		}
		_t& data()
		{
			return m_data;
		}

	private:
		_t m_data;
		bool m_is_valid = false;
		node_idx m_parent = k_invalid;
		node_idx m_first_child = k_invalid;
		node_idx m_next_sibling = k_invalid;
		node_idx m_prev_sibling = k_invalid;
		node_idx m_idx;
		bool m_visited = false;

		flatgraph* m_graph;
		friend class flatgraph;
	};

	const node& get(node_idx index) const
	{
		return m_nodes[index];
	}

	node& get(node_idx index)
	{
		return m_nodes[index];
	}

	node_idx add_node(const _t& data, node_idx parent_index = k_root)
	{
		const node_idx new_idx = allocate_node();

		node& parent_node = m_nodes[parent_index];
		const bool is_first_child = !has_children(parent_index);
		if (is_first_child)
		{
			parent_node.m_first_child = new_idx;
		}

		node& new_node = m_nodes[new_idx];
		new_node.m_parent = (node_idx)parent_index;
		new_node.m_first_child = k_invalid;
		new_node.m_is_valid = true;
		new_node.m_data = data;

		// prev <- new -> next
		const node_idx parent_first_child_index = first_child(parent_index);
		new_node.m_prev_sibling = is_first_child ? new_idx : last_child(parent_index);
		new_node.m_next_sibling = parent_first_child_index;
		// prev -> new
		node& prev_node = m_nodes[new_node.m_prev_sibling];
		prev_node.m_next_sibling = new_idx;
		// new <- first
		node& first_node = m_nodes[parent_first_child_index];
		first_node.m_prev_sibling = new_idx;

		return new_idx;
	}

	template <typename _fn>
	void traverse(_fn&& func, node_idx start_node = k_root, traverse_mode mode = traverse_mode::width) const
	{
		vector<bool> visited_map{};
		visited_map.resize(m_nodes.size());

		// first visit start node
		visited_map[start_node] = true;
		func(start_node);

		if (mode == traverse_mode::width)
		{
			std::function<void(node_idx)> traverse_layer;
			traverse_layer = [this, &traverse_layer, &func, &visited_map](node_idx layer_start)
				{
					if (layer_start == k_invalid) return;

					node_idx current = layer_start;
					// traverse each node in the layer
					do
					{
						// visit the node
						if (visited_map[current] == false)
						{
							func(current);
						}
						visited_map[current] = true;

						current = next_sib(current);

					} while (current != layer_start);

					// reset current and now traverse the child layer of each node
					current = layer_start;
					do
					{
						traverse_layer(first_child(current));
						current = next_sib(current);
					} while (current != layer_start);
				};

			traverse_layer(first_child(start_node));
		}
	}

	template <typename _pred>
	node_idx find_child_node(node_idx start, _pred&& predicate)
	{
		node_idx found = k_invalid;
		traverse(start, traverse_mode::width, [&predicate, &found](const node_idx& idx)
			{
				if (found == k_invalid && predicate(idx))
				{
					found = idx;
				}
			});
		return found;
	}

private:
	uint32 m_first_inactive = 0u;
	vector<node> m_nodes{};
	node_idx allocate_node()
	{
		m_nodes.push_back({});
		const node_idx new_idx = (node_idx)m_nodes.size() - 1u;
		m_nodes.back().m_graph = this;
		m_nodes.back().m_idx = new_idx;
		m_first_inactive = (uint32)m_nodes.size();
		return new_idx;
	}
};
} // strikers