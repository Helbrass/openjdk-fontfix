package swinglabel;

import java.awt.Color;
import java.awt.Font;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import javax.swing.BorderFactory;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;

public class Swinglabel implements Runnable {

    public static void main(String[] args) {
        System.setProperty("swing.boldMetal", "false");
        SwingUtilities.invokeLater(new Swinglabel());
    }
    
    private JPanel createContentPane() {
        JPanel panel = new JPanel(new GridBagLayout());
        panel.setBackground(Color.WHITE);
        panel.setBorder(BorderFactory.createEmptyBorder(12, 12, 12, 12));
        
        GridBagConstraints c = new GridBagConstraints();
        c.gridx = 0;
        c.gridy = 0;
        c.fill = GridBagConstraints.HORIZONTAL;
        c.anchor = GridBagConstraints.FIRST_LINE_START;
        c.weightx = 1;
        
        panel.add(createLabel("DejaVu Sans Mono", 10), c);
        c.gridy++;
        panel.add(createLabel("DejaVu Sans Mono", 12), c);
        c.gridy++;
        panel.add(createLabel("Consolas", 10), c);
        c.gridy++;
        panel.add(createLabel("Consolas", 12), c);
        c.gridy++;
        panel.add(createLabel(Font.DIALOG, 0), c);
        
        return panel;
    }
    
    private JLabel createLabel(String fontFamily, int fontSize) {
        JLabel label = new JLabel("The quick brown fox jumps over the lazy dog (" + fontFamily + (fontSize == 0 ? ")" : " " + fontSize + ")"));
        Font font = new Font(fontFamily, Font.PLAIN, fontSize == 0 ? label.getFont().getSize() : fontSize);
        label.setFont(font);
        return label;
    }

    @Override
    public void run() {
        JFrame frame = new JFrame("swinglabel");
        frame.setContentPane(createContentPane());
        frame.setBackground(Color.WHITE);
        frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
        frame.pack();
        frame.setLocationRelativeTo(null);
        frame.setVisible(true);
    }
}
