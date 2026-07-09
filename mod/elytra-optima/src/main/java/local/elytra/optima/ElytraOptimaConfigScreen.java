package local.elytra.optima;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

public final class ElytraOptimaConfigScreen extends Screen {
    private static final int ROW_HEIGHT = 22;
    private static final int BUTTON_HEIGHT = 18;
    private static final int TOP_Y = 48;

    private final Screen parent;
    private List<String> cycleOrder;
    private String defaultStrategyId;
    private String statusMessage = "";
    private int statusColor = 0xA0FFA0;
    private int scrollOffset = 0;

    public ElytraOptimaConfigScreen(Screen parent) {
        super(Component.literal("Elytra Optima"));
        this.parent = parent;
        reloadState();
    }

    @Override
    protected void init() {
        boolean chinese = isChinese();
        List<ElytraOptimaClient.StrategyInfo> rows = orderedRows(chinese);
        clampScroll(rows.size());

        int panelWidth = panelWidth();
        int left = panelLeft(panelWidth);
        int buttonLeft = left + panelWidth - 188;
        int y = TOP_Y;
        int visibleRows = visibleRows();
        int end = Math.min(rows.size(), scrollOffset + visibleRows);

        for (int rowIndex = scrollOffset; rowIndex < end; rowIndex++) {
            ElytraOptimaClient.StrategyInfo info = rows.get(rowIndex);
            String id = info.id();
            boolean enabled = cycleOrder.contains(id);
            int index = cycleOrder.indexOf(id);

            addRenderableWidget(Button.builder(
                Component.literal(enabled ? text(chinese, "启用", "On") : text(chinese, "关闭", "Off")),
                button -> toggleStrategy(id)
            ).bounds(buttonLeft, y, 38, BUTTON_HEIGHT).build());

            Button defaultButton = Button.builder(
                Component.literal(id.equals(defaultStrategyId) ? text(chinese, "默认", "Default") : text(chinese, "设默认", "Set")),
                button -> setDefaultStrategy(id)
            ).bounds(buttonLeft + 42, y, 54, BUTTON_HEIGHT).build();
            defaultButton.active = enabled && !id.equals(defaultStrategyId);
            addRenderableWidget(defaultButton);

            Button upButton = Button.builder(Component.literal(text(chinese, "上移", "Up")), button -> moveStrategy(id, -1))
                .bounds(buttonLeft + 100, y, 40, BUTTON_HEIGHT)
                .build();
            upButton.active = enabled && index > 0;
            addRenderableWidget(upButton);

            Button downButton = Button.builder(Component.literal(text(chinese, "下移", "Down")), button -> moveStrategy(id, 1))
                .bounds(buttonLeft + 144, y, 44, BUTTON_HEIGHT)
                .build();
            downButton.active = enabled && index >= 0 && index < cycleOrder.size() - 1;
            addRenderableWidget(downButton);

            y += ROW_HEIGHT;
        }

        int bottomY = this.height - 28;
        addRenderableWidget(Button.builder(Component.literal(text(chinese, "恢复默认", "Reset")), button -> resetDefaults())
            .bounds(this.width / 2 - 124, bottomY, 118, BUTTON_HEIGHT)
            .build());
        addRenderableWidget(Button.builder(Component.literal(text(chinese, "完成", "Done")), button -> onClose())
            .bounds(this.width / 2 + 6, bottomY, 118, BUTTON_HEIGHT)
            .build());
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
        super.extractRenderState(graphics, mouseX, mouseY, partialTick);

        boolean chinese = isChinese();
        int panelWidth = panelWidth();
        int left = panelLeft(panelWidth);
        int buttonLeft = left + panelWidth - 188;
        int y = TOP_Y;
        int labelX = left + 8;
        int labelWidth = Math.max(40, buttonLeft - labelX - 8);

        graphics.centeredText(this.font, this.title, this.width / 2, 18, 0xFFFFFF);
        graphics.centeredText(
            this.font,
            Component.literal(text(chinese, "选择 J 键切换顺序，以及 H 开启时的默认策略", "Choose J cycle order and the default strategy selected by H")),
            this.width / 2,
            34,
            0xA0A0A0
        );

        List<ElytraOptimaClient.StrategyInfo> rows = orderedRows(chinese);
        clampScroll(rows.size());
        int visibleRows = visibleRows();
        int end = Math.min(rows.size(), scrollOffset + visibleRows);
        for (int rowIndex = scrollOffset; rowIndex < end; rowIndex++) {
            ElytraOptimaClient.StrategyInfo info = rows.get(rowIndex);
            String id = info.id();
            int index = cycleOrder.indexOf(id);
            String prefix = index >= 0 ? (index + 1) + ". " : "- ";
            int color = index >= 0 ? 0xFFFFFF : 0x777777;
            String label = truncate(prefix + info.label(), labelWidth);
            graphics.text(this.font, label, labelX, y + 5, color, true);
            y += ROW_HEIGHT;
        }

        if (rows.size() > visibleRows) {
            String page = (scrollOffset + 1) + "-" + end + " / " + rows.size();
            graphics.centeredText(this.font, Component.literal(page), this.width / 2, this.height - 48, 0xA0A0A0);
        }

        if (!statusMessage.isEmpty()) {
            graphics.centeredText(this.font, Component.literal(statusMessage), this.width / 2, this.height - 60, statusColor);
        }
    }

    @Override
    public void onClose() {
        Minecraft.getInstance().setScreenAndShow(parent);
    }

    @Override
    public boolean mouseScrolled(double mouseX, double mouseY, double horizontalAmount, double verticalAmount) {
        int oldOffset = scrollOffset;
        if (verticalAmount < 0.0) {
            scrollOffset++;
        } else if (verticalAmount > 0.0) {
            scrollOffset--;
        }
        clampScroll(orderedRows(isChinese()).size());
        if (oldOffset != scrollOffset) {
            rebuildWidgets();
            return true;
        }
        return super.mouseScrolled(mouseX, mouseY, horizontalAmount, verticalAmount);
    }

    private void toggleStrategy(String id) {
        if (cycleOrder.contains(id)) {
            if (cycleOrder.size() <= 1) {
                setStatus(text(isChinese(), "至少保留一个策略", "Keep at least one strategy"), false);
                return;
            }
            cycleOrder.remove(id);
            if (id.equals(defaultStrategyId)) {
                defaultStrategyId = cycleOrder.get(0);
            }
        } else {
            cycleOrder.add(id);
            if (defaultStrategyId == null || defaultStrategyId.isBlank()) {
                defaultStrategyId = id;
            }
        }
        saveAndRefresh();
    }

    private void setDefaultStrategy(String id) {
        if (!cycleOrder.contains(id)) {
            cycleOrder.add(id);
        }
        defaultStrategyId = id;
        saveAndRefresh();
    }

    private void moveStrategy(String id, int direction) {
        int index = cycleOrder.indexOf(id);
        int target = index + direction;
        if (index < 0 || target < 0 || target >= cycleOrder.size()) {
            return;
        }
        String moved = cycleOrder.remove(index);
        cycleOrder.add(target, moved);
        saveAndRefresh();
    }

    private void resetDefaults() {
        boolean ok = ElytraOptimaClient.resetConfigToDefaults();
        reloadState();
        setStatus(text(isChinese(), "已恢复默认配置", "Defaults restored"), ok);
        rebuildWidgets();
    }

    private void saveAndRefresh() {
        boolean ok = ElytraOptimaClient.configureStrategies(defaultStrategyId, cycleOrder);
        reloadState();
        setStatus(text(isChinese(), "已保存", "Saved"), ok);
        rebuildWidgets();
    }

    private void reloadState() {
        this.cycleOrder = new ArrayList<>(ElytraOptimaClient.configuredCycleOrderIds());
        this.defaultStrategyId = ElytraOptimaClient.configuredDefaultStrategyId();
    }

    private List<ElytraOptimaClient.StrategyInfo> orderedRows(boolean chinese) {
        List<ElytraOptimaClient.StrategyInfo> all = ElytraOptimaClient.strategyInfos(chinese);
        Map<String, ElytraOptimaClient.StrategyInfo> byId = new HashMap<>();
        for (ElytraOptimaClient.StrategyInfo info : all) {
            byId.put(info.id(), info);
        }

        List<ElytraOptimaClient.StrategyInfo> rows = new ArrayList<>();
        Set<String> used = new HashSet<>();
        for (String id : cycleOrder) {
            ElytraOptimaClient.StrategyInfo info = byId.get(id);
            if (info != null && used.add(id)) {
                rows.add(info);
            }
        }
        for (ElytraOptimaClient.StrategyInfo info : all) {
            if (used.add(info.id())) {
                rows.add(info);
            }
        }
        return rows;
    }

    private void setStatus(String message, boolean success) {
        this.statusMessage = message;
        this.statusColor = success ? 0xA0FFA0 : 0xFF8080;
    }

    private int panelWidth() {
        return Math.min(520, Math.max(260, this.width - 24));
    }

    private int panelLeft(int panelWidth) {
        return (this.width - panelWidth) / 2;
    }

    private int visibleRows() {
        return Math.max(1, (this.height - TOP_Y - 58) / ROW_HEIGHT);
    }

    private void clampScroll(int rowCount) {
        int maxOffset = Math.max(0, rowCount - visibleRows());
        if (scrollOffset < 0) {
            scrollOffset = 0;
        } else if (scrollOffset > maxOffset) {
            scrollOffset = maxOffset;
        }
    }

    private String truncate(String value, int maxWidth) {
        if (this.font.width(value) <= maxWidth) {
            return value;
        }
        String ellipsis = "...";
        int target = Math.max(0, maxWidth - this.font.width(ellipsis));
        return this.font.plainSubstrByWidth(value, target) + ellipsis;
    }

    private boolean isChinese() {
        String selected = Minecraft.getInstance().getLanguageManager().getSelected();
        return selected != null && selected.toLowerCase(Locale.ROOT).startsWith("zh");
    }

    private static String text(boolean chinese, String zh, String en) {
        return chinese ? zh : en;
    }
}
