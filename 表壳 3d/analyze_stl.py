#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STL 分析脚本 – 用于原始智能手表表壳几何分析
用法：
    python analyze_stl.py <stl_file>
    python analyze_stl.py <stl_file> --output case_analysis.txt
    python analyze_stl.py <stl_file> --json case_analysis.json
"""

import argparse
import sys
import json
from pathlib import Path

try:
    import trimesh
    import numpy as np
except ImportError as e:
    print("错误：请先安装 trimesh 和 numpy")
    print("   pip install trimesh numpy")
    sys.exit(1)


def analyze_stl(stl_path):
    """执行完整分析，返回结果字典"""
    result = {
        "file_info": {},
        "mesh_integrity": {},
        "bounding_box": {},
        "geometry_estimates": {},
        "volume_surface": {},
        "section_analysis": {},
        "anomalies": [],
    }

    # ---- 读取 STL ----
    mesh = trimesh.load(stl_path)
    if mesh is None:
        raise ValueError("无法读取 STL 文件，请确认格式正确")

    # 如果是场景或多物体，合并
    if isinstance(mesh, trimesh.Scene):
        mesh = trimesh.util.concatenate(
            [geom for geom in mesh.geometry.values() if isinstance(geom, trimesh.Trimesh)]
        )
    if not isinstance(mesh, trimesh.Trimesh):
        raise ValueError("未能解析为 Trimesh 对象")

    result["file_info"] = {
        "readable": True,
        "format": Path(stl_path).suffix.lower(),
        "triangles": len(mesh.faces),
        "vertices": len(mesh.vertices),
    }

    # ---- 网格完整性 ----
    integrity = {}
    # 是否为封闭实体
    integrity["is_watertight"] = mesh.is_watertight
    # 边界边数量（使用 boundary_edges）
    try:
        integrity["boundary_edges"] = len(mesh.boundary_edges)
    except AttributeError:
        # 兼容旧版本
        integrity["boundary_edges"] = len(mesh.edges[mesh.edges_boundary]) if hasattr(mesh, 'edges_boundary') else 0
    # non-manifold 边
    try:
        integrity["non_manifold_edges"] = len(mesh.non_manifold_edges)
    except AttributeError:
        integrity["non_manifold_edges"] = len(mesh.edges[mesh.edges_non_manifold]) if hasattr(mesh, 'edges_non_manifold') else 0
    # 退化三角形（布尔数组，求 sum）
    if hasattr(mesh, 'degenerate_faces'):
        integrity["degenerate_faces"] = int(np.sum(mesh.degenerate_faces))
    else:
        integrity["degenerate_faces"] = 0
    # 是否有开放边界
    integrity["has_open_boundary"] = integrity["boundary_edges"] > 0
    result["mesh_integrity"] = integrity

    # ---- 包围盒 ----
    bbox = mesh.bounds
    min_vals = bbox[0]
    max_vals = bbox[1]
    sizes = max_vals - min_vals
    result["bounding_box"] = {
        "min": {"x": float(min_vals[0]), "y": float(min_vals[1]), "z": float(min_vals[2])},
        "max": {"x": float(max_vals[0]), "y": float(max_vals[1]), "z": float(max_vals[2])},
        "size": {"x": float(sizes[0]), "y": float(sizes[1]), "z": float(sizes[2])},
    }

    # ---- 几何尺寸估算 ----
    diam_xy = max(sizes[0], sizes[1])
    diam_min = min(sizes[0], sizes[1])
    height_z = sizes[2]
    axis_aligned = abs(sizes[0] - sizes[1]) / max(sizes[0], sizes[1]) < 0.1
    result["geometry_estimates"] = {
        "max_diameter_xy": float(diam_xy),
        "min_diameter_xy": float(diam_min),
        "height_z": float(height_z),
        "is_axis_aligned_z": bool(axis_aligned),
        "principal_axis": "Z" if axis_aligned else "unknown",
        "highest_point_z": float(max_vals[2]),
        "lowest_point_z": float(min_vals[2]),
    }

    # ---- 体积与表面积 ----
    if mesh.is_watertight:
        try:
            volume = mesh.volume
            surface = mesh.area
            result["volume_surface"] = {
                "volume": float(volume),
                "surface_area": float(surface),
                "valid": True,
            }
        except:
            result["volume_surface"] = {"valid": False, "reason": "计算失败"}
    else:
        result["volume_surface"] = {"valid": False, "reason": "网格不是封闭实体，无法可靠计算体积"}

    # ---- 分层截面分析 ----
    z_min, z_max = min_vals[2], max_vals[2]
    z_levels = np.linspace(z_min, z_max, 11)
    sections = []
    for i, z in enumerate(z_levels):
        percentage = i * 10
        try:
            slice_path = mesh.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
            if slice_path is not None:
                points = slice_path.vertices[:, :2]
                if len(points) > 2:
                    try:
                        hull = trimesh.geometry.convex_hull(points)
                        area = trimesh.geometry.polygon_area(hull)
                        eq_diameter = 2 * np.sqrt(area / np.pi) if area > 0 else 0
                    except:
                        area = 0
                        eq_diameter = 0
                else:
                    area = 0
                    eq_diameter = 0
                sections.append({
                    "z": float(z),
                    "percentage": percentage,
                    "has_section": True,
                    "area": float(area),
                    "equivalent_diameter": float(eq_diameter),
                })
            else:
                sections.append({
                    "z": float(z),
                    "percentage": percentage,
                    "has_section": False,
                    "area": 0,
                    "equivalent_diameter": 0,
                })
        except Exception as e:
            sections.append({
                "z": float(z),
                "percentage": percentage,
                "has_section": False,
                "error": str(e),
            })
    result["section_analysis"] = sections

    # ---- 几何异常提示 ----
    anomalies = []
    if len(sections) >= 3:
        low_10 = sections[1]
        low_20 = sections[2]
        if low_10["has_section"] and low_20["has_section"]:
            if low_10["equivalent_diameter"] < low_20["equivalent_diameter"] * 0.7:
                anomalies.append("底部可能存在凸起或收窄 (直径在10%高度处明显小于20%高度)")
    if len(sections) >= 3:
        top_90 = sections[-2]
        top_100 = sections[-1]
        if top_90["has_section"] and top_100["has_section"]:
            if top_100["equivalent_diameter"] < top_90["equivalent_diameter"] * 0.6:
                anomalies.append("顶部可能存在屏幕或明显收窄 (100%高度直径远小于90%高度)")
    anomalies.append("注意：STL 网格无法可靠判断传感器、触点、按键等电子功能区域，需实物或照片确认。")
    result["anomalies"] = anomalies

    return result


def main():
    parser = argparse.ArgumentParser(description="分析 STL 文件的几何特征")
    parser.add_argument("stl", help="输入 STL 文件路径")
    parser.add_argument("--output", help="输出文本文件路径")
    parser.add_argument("--json", help="输出 JSON 文件路径")
    args = parser.parse_args()

    stl_path = Path(args.stl)
    if not stl_path.exists():
        print(f"错误：文件 {stl_path} 不存在")
        sys.exit(1)

    try:
        result = analyze_stl(stl_path)
    except Exception as e:
        print(f"分析失败：{e}")
        sys.exit(1)

    print(json.dumps(result, indent=2, ensure_ascii=False))

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write("STL 分析结果\n")
            f.write("=============\n")
            for key, val in result.items():
                f.write(f"\n{key}:\n")
                f.write(json.dumps(val, indent=2, ensure_ascii=False) + "\n")
        print(f"文本报告已保存至 {args.output}")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2, ensure_ascii=False)
        print(f"JSON 报告已保存至 {args.json}")


if __name__ == "__main__":
    main()
